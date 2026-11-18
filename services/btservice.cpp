#include "btservice.h"

#include "appsettings.h"
#include "btworker.h"
#include "commandspec.h"
#include "ibtbackend.h"
#include "logpanel.h"
#include "processrunner.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QMetaType>
#include <QThread>
#include <QTimer>

namespace {

// 配对成功的兜底轮询：pair 之后等这么久还没等到 BONDED，就主动查一次
// 已配对列表。原因见 BtService 类注释——BONDED 回调经常永远不来。
const int kBondWatchdogMs = 15000;

} // namespace

BtService *BtService::instance()
{
    return hmi::singleton<BtService>([]() { return new BtService; });
}

BtService::BtService(QObject *parent)
    : DeviceService(QStringLiteral("bt"), QObject::tr("蓝牙"), 15000, parent)
    , m_thread(nullptr)
    , m_worker(nullptr)
    , m_bondWatchdog(new QTimer(this))
    , m_adapterOn(false)
    , m_scanning(false)
    , m_busy(false)
    , m_initialized(false)
{
    // 跨线程信号要传自定义类型，必须先注册。
    // quintptr：配对确认时从 bluez mainloop 线程带回 GDBusMethodInvocation 句柄，
    // 不注册的话 Qt 会报 "Cannot queue arguments of type 'quintptr'" 并**丢弃**
    // 整个 queued 调用——表现为配对确认面板永远不弹。
    qRegisterMetaType<BtDeviceRecord>("BtDeviceRecord");
    qRegisterMetaType<QList<BtDeviceRecord> >("QList<BtDeviceRecord>");
    qRegisterMetaType<quintptr>("quintptr");

    m_bondWatchdog->setSingleShot(true);
    m_bondWatchdog->setInterval(kBondWatchdogMs);
    connect(m_bondWatchdog, &QTimer::timeout, this, &BtService::onBondWatchdog);

    startWorker();
    publish(DeviceOff, tr("未初始化"));
    refresh();
}

BtService::~BtService()
{
    if (m_thread) {
        // shutdown 必须在工作线程上跑：deinit 同样走 act_transfer 同步投递
        emit requestShutdown();
        m_thread->quit();
        m_thread->wait(3000);
    }
}

QString BtService::backendName() const
{
    // 后端实例在工作线程上，GUI 线程不碰它；这里只报编译期信息。
    return btBackendCompiledIn() ? QStringLiteral("btmg") : QStringLiteral("fake");
}

void BtService::startWorker()
{
    const bool simulated = AppSettings::instance()->simulated();

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("btWorker"));
    m_worker = new BtWorker(simulated);
    m_worker->moveToThread(m_thread);

    // GUI → 工作线程
    connect(this, &BtService::requestInitialize, m_worker, &BtWorker::initialize);
    connect(this, &BtService::requestEnable, m_worker, &BtWorker::setEnabled);
    connect(this, &BtService::requestScan, m_worker, &BtWorker::startScan);
    connect(this, &BtService::requestStopScan, m_worker, &BtWorker::stopScan);
    connect(this, &BtService::requestPair, m_worker, &BtWorker::pair);
    connect(this, &BtService::requestUnpair, m_worker, &BtWorker::unpair);
    connect(this, &BtService::requestConnect, m_worker, &BtWorker::connectDevice);
    connect(this, &BtService::requestDisconnect, m_worker, &BtWorker::disconnectDevice);
    connect(this, &BtService::requestReloadPaired, m_worker, &BtWorker::reloadPaired);
    connect(this, &BtService::requestReplyPasskey, m_worker, &BtWorker::replyPasskey);
    connect(this, &BtService::requestShutdown, m_worker, &BtWorker::shutdown);

    // 工作线程 → GUI（跨线程自动 QueuedConnection）
    connect(m_worker, &BtWorker::adapterState, this, &BtService::onAdapterState);
    connect(m_worker, &BtWorker::scanState, this, &BtService::onScanState);
    connect(m_worker, &BtWorker::deviceFound, this, &BtService::onDeviceFound);
    connect(m_worker, &BtWorker::deviceRemoved, this, &BtService::onDeviceRemoved);
    connect(m_worker, &BtWorker::rssiUpdated, this, &BtService::onRssiUpdated);
    connect(m_worker, &BtWorker::bondState, this, &BtService::onBondState);
    connect(m_worker, &BtWorker::passkeyRequested, this, &BtService::onPasskeyRequested);
    connect(m_worker, &BtWorker::pairedList, this, &BtService::onPairedList);
    connect(m_worker, &BtWorker::log, this, &BtService::onBackendLog);
    connect(m_worker, &BtWorker::localAddressResolved, this,
            [this](const QString &address) { m_localAddress = address; });

    // 线程结束时销毁 worker（worker 没有 parent，必须显式删）
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();

    // ── 初始化 ──────────────────────────────────────────────────
    // 只做 preinit / profile / 回调注册 / init，不碰硬件，所以不必等
    // 板端那段耗时的链路。profile 用 0 表示「完全跟随 bluetooth.json」，
    // 板端 json 现在是 a2dp_sink=1 + avrcp=1。
    emit requestInitialize(0);
    m_initialized = true;
}

void BtService::refresh()
{
    // 关键：这里不调用任何 bt_manager_*。
    // ServiceRegistry 每 15s 在 GUI 线程调一次本方法，
    // 而 libbtmg 的任意调用都可能排在同一条 action 队列后面等 15s（connect 期间），
    // 在 GUI 线程上等就是冻界面。这里只把已知状态摊一遍。
    if (!m_adapterOn) {
        publish(DeviceOff, m_busy ? tr("处理中") : tr("未上电"));
        return;
    }
    if (m_scanning)
        publish(DeviceBusy, tr("扫描中"));
    else
        publish(DeviceReady, tr("控制器就绪"));
}

void BtService::powerOn()
{
    if (m_busy) {
        LogPanel::post(tr("蓝牙正在处理上一个操作，请稍候"), LogPanel::Warn);
        return;
    }
    m_busy = true;
    publish(DeviceBusy, tr("上电中"));
    LogPanel::post(tr("蓝牙上电中（真机实测约 4 秒）"), LogPanel::Info);
    emit requestEnable(true);
}

void BtService::powerOff()
{
    if (m_busy) {
        LogPanel::post(tr("蓝牙正在处理上一个操作，请稍候"), LogPanel::Warn);
        return;
    }
    m_busy = true;
    publish(DeviceBusy, tr("下电中"));
    emit requestEnable(false);
}

void BtService::scan()
{
    if (!m_adapterOn) {
        LogPanel::post(tr("蓝牙未上电，无法扫描"), LogPanel::Warn);
        return;
    }
    if (m_scanning)
        return;

    // 对齐库的行为：bt_adapter_start_scan 首句会 bt_remove_unpaired_devices()，
    // 已发现未配对的设备会被清掉，所以每次开始扫描必须清空本地列表重来。
    emit requestScan(int(BtScanClassic));
}

void BtService::stopScan()
{
    if (!m_scanning)
        return;
    emit requestStopScan();
}

void BtService::pair(const QString &address)
{
    if (!m_adapterOn) {
        LogPanel::post(tr("蓝牙未上电，无法配对"), LogPanel::Warn);
        return;
    }

    m_pendingPairAddress = address;
    emit requestPair(address);

    // 兜底：BONDED 回调经常不来（见类注释），15s 后主动查一次已配对列表
    m_bondWatchdog->start();
}

void BtService::unpair(const QString &address)
{
    emit requestUnpair(address);
}

void BtService::connectDevice(const QString &address)
{
    if (m_busy) {
        LogPanel::post(tr("蓝牙正在处理上一个操作，请稍候"), LogPanel::Warn);
        return;
    }
    // connect 的 D-Bus 超时是 15s（bt_device.c:123），期间同一条 action 队列
    // 上的其它调用都会排队，所以这里也置 busy 挡住用户连续点击。
    m_busy = true;
    publish(DeviceBusy, tr("连接中"));
    emit requestConnect(address);
}

void BtService::disconnectDevice(const QString &address)
{
    emit requestDisconnect(address);
}

void BtService::confirmPairing(quintptr handle, bool accept)
{
    emit requestReplyPasskey(handle, accept);
}

void BtService::reloadPaired()
{
    emit requestReloadPaired();
}

void BtService::onAdapterState(int state)
{
    Q_UNUSED(state)
    // 真实现里 TurningOn/TurningOff 是中间态，这里以 On/Off 为准。
    // 具体值由后端判定后经 adapterStateChanged 出来，这里只做状态位同步。
    const bool on = (state == BtAdapterOn || state == BtAdapterTurningOn);
    const bool settled = (state == BtAdapterOn || state == BtAdapterOff);

    if (settled) {
        m_adapterOn = on;
        m_busy = false;
        emit adapterStateChanged(on);
        publish(on ? DeviceReady : DeviceOff, on ? tr("控制器就绪") : tr("已关闭"));
    } else {
        publish(DeviceBusy, on ? tr("上电中") : tr("下电中"));
    }

    if (!on) {
        m_scanning = false;
        m_pendingPairAddress.clear();
        m_bondWatchdog->stop();
    }
}

void BtService::onScanState(int state)
{
    m_scanning = (state == BtScanStarted);
    emit scanStateChanged(state);
    if (m_scanning)
        publish(DeviceBusy, tr("扫描中"));
    else
        publish(DeviceReady, tr("扫描结束"));
}

void BtService::onDeviceFound(const BtDeviceRecord &device)
{
    emit deviceFound(device);
}

void BtService::onDeviceRemoved(const QString &address)
{
    emit deviceRemoved(address);
}

void BtService::onRssiUpdated(const QString &address, int rssi)
{
    emit rssiUpdated(address, rssi);
}

void BtService::onBondState(int state, const QString &address)
{
    emit bondStateChanged(state, address);

    switch (state) {
    case BtBondBonding:
        publish(DeviceBusy, tr("配对中 · %1").arg(address));
        break;
    case BtBondBonded:
        m_bondWatchdog->stop();
        m_pendingPairAddress.clear();
        LogPanel::post(tr("配对成功：%1").arg(address), LogPanel::Ok);
        publish(DeviceReady, tr("已配对 · %1").arg(address));
        reloadPaired();
        break;
    case BtBondBondFailed:
        m_bondWatchdog->stop();
        m_pendingPairAddress.clear();
        LogPanel::post(tr("配对失败：%1").arg(address), LogPanel::Error);
        publish(DeviceReady, tr("配对失败"));
        break;
    default:
        break;
    }
}

void BtService::onPasskeyRequested(quintptr handle, const QString &address,
                                   unsigned int passkey, bool numericOnly)
{
    // 直接把 handle 透给界面。界面必须尽快（20s 内）调 confirmPairing()，
    // 否则 BlueZ 侧超时；这个兜底由界面自己起定时器做，
    // 服务层不方便替它决定「超时了要不要自动拒」。
    emit passkeyRequested(handle, address, passkey, numericOnly);
}

void BtService::onPairedList(const QList<BtDeviceRecord> &devices)
{
    emit pairedListChanged(devices);
}

void BtService::onBackendLog(const QString &text, int level)
{
    LogPanel::Level mapped = LogPanel::Info;
    if (level == 1)
        mapped = LogPanel::Warn;
    else if (level >= 2)
        mapped = LogPanel::Error;
    LogPanel::post(QStringLiteral("[BT] ") + text, mapped);
}

void BtService::onBondWatchdog()
{
    if (m_pendingPairAddress.isEmpty())
        return;

    const QString address = m_pendingPairAddress;
    m_pendingPairAddress.clear();

    // 不在这里下结论——BONDED 可能只是没来，不代表失败。
    // 让界面去刷新已配对列表，由列表里有没有该地址来判断。
    LogPanel::post(tr("配对结果未回传，重新读取已配对列表（%1）").arg(address),
                   LogPanel::Warn);
    reloadPaired();
}

HMI_REGISTER_SERVICE("bt", BtService)
