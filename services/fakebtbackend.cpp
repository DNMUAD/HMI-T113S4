#include "fakebtbackend.h"

#include <QTimer>

namespace {

// 发现节奏：每 400ms 抛一个设备，模拟真机的陆续发现
const int kScanTickMs = 400;
// 真机实测 3.7 秒（板端 2026-11-18 日志）；这里 1500ms 足够看清「处理中 → 就绪」
const int kEnableMs = 1500;
// 配对确认到结果之间给 UI 留的反应时间
const int kPairMs = 600;

} // namespace

FakeBtBackend::FakeBtBackend(QObject *parent)
    : QObject(parent)
    , m_enableTimer(new QTimer(this))
    , m_scanTimer(new QTimer(this))
    , m_pairTimer(new QTimer(this))
    , m_initialized(false)
    , m_enabled(false)
    , m_pendingEnable(false)
    , m_scanning(false)
    , m_scanTicks(0)
    , m_profileMask(0)
{
    m_enableTimer->setSingleShot(true);
    connect(m_enableTimer, &QTimer::timeout, this, &FakeBtBackend::onEnableDone);

    m_scanTimer->setInterval(kScanTickMs);
    connect(m_scanTimer, &QTimer::timeout, this, &FakeBtBackend::onScanTick);

    m_pairTimer->setSingleShot(true);
    connect(m_pairTimer, &QTimer::timeout, this, &FakeBtBackend::onPairTimerFired);
}

FakeBtBackend::~FakeBtBackend()
{
}

void FakeBtBackend::setEvents(const Events &events)
{
    m_events = events;
}

void FakeBtBackend::emitLog(const QString &text, int level)
{
    if (m_events.log)
        m_events.log(text, level);
}

bool FakeBtBackend::initialize(int profileMask)
{
    m_profileMask = profileMask;
    m_initialized = true;
    m_status = QStringLiteral("模拟后端就绪");

    // 预置「已配对」两只，其中一只刻意不给名字：
    // 真实现里 bluez_app_callback_handle 会因为 r_class == 0 而吞掉 BONDED 回调，
    // 界面对「已配对但拿不到中文名」这种情况必须有兜底显示。
    m_paired.clear();
    m_paired << makeDevice(QStringLiteral("AC:12:2F:3A:4B:5C"),
                           QStringLiteral("SoundCore Mini"),
                           QStringLiteral("audio-card"), 0, true, false, 0x240404);
    m_paired << makeDevice(QStringLiteral("00:1A:7D:DA:71:13"),
                           QString(), QStringLiteral("computer"), 0, true, false, 0);

    emitLog(QStringLiteral("[模拟] 蓝牙后端初始化，profile mask=0x%1")
            .arg(profileMask, 0, 16), 0);
    return true;
}

bool FakeBtBackend::setEnabled(bool enabled)
{
    if (!m_initialized) {
        m_status = QStringLiteral("后端未初始化");
        return false;
    }
    if (m_pendingEnable) {
        m_status = QStringLiteral("正在切换上电状态，请稍候");
        return false;
    }

    m_pendingEnable = true;
    m_enabled = enabled;
    m_status = enabled ? QStringLiteral("上电中") : QStringLiteral("下电中");

    if (m_events.adapterState)
        m_events.adapterState(enabled ? BtAdapterTurningOn : BtAdapterTurningOff);

    emitLog(enabled ? QStringLiteral("[模拟] 开始上电（真机实测约 4 秒）")
                    : QStringLiteral("[模拟] 开始下电"), 0);

    m_enableTimer->start(kEnableMs);
    return true;
}

void FakeBtBackend::onEnableDone()
{
    const bool enabled = m_enabled;
    m_pendingEnable = false;

    if (!enabled) {
        m_scanning = false;
        m_scanTimer->stop();
    }

    m_status = enabled ? QStringLiteral("控制器就绪") : QStringLiteral("已关闭");
    if (m_events.adapterState)
        m_events.adapterState(enabled ? BtAdapterOn : BtAdapterOff);
    emitLog(enabled ? QStringLiteral("[模拟] 上电完成") : QStringLiteral("[模拟] 下电完成"), 0);
}

bool FakeBtBackend::isEnabled() const
{
    return m_enabled && !m_pendingEnable;
}

bool FakeBtBackend::startScan(int scanType)
{
    if (!isEnabled()) {
        m_status = QStringLiteral("未上电，无法扫描");
        return false;
    }
    if (m_scanning)
        return true;

    // 对齐真实现的行为：bt_adapter_start_scan 首句就是 bt_remove_unpaired_devices()，
    // 已发现未配对的会被清掉，所以本地列表每次重来。
    m_devices.clear();
    m_scanning = true;
    m_scanTicks = 0;

    m_queue.clear();
    m_queue << QStringLiteral("58:2F:40:11:22:33")
            << QStringLiteral("7C:9E:BD:44:55:66")
            << QStringLiteral("A4:C1:38:77:88:99")
            << QStringLiteral("F0:18:98:AA:BB:CC");

    if (m_events.scanState)
        m_events.scanState(BtScanStarted);
    emitLog(QStringLiteral("[模拟] 开始扫描（type=%1）").arg(scanType), 0);

    m_scanTimer->start();
    return true;
}

void FakeBtBackend::onScanTick()
{
    if (!m_scanning) {
        m_scanTimer->stop();
        return;
    }

    if (m_queue.isEmpty()) {
        // 抛完就自动停，模拟真机 discovery 结束
        m_scanning = false;
        m_scanTimer->stop();
        if (m_events.scanState)
            m_events.scanState(BtScanStopped);
        emitLog(QStringLiteral("[模拟] 扫描结束，共 %1 台").arg(m_devices.size()), 0);
        return;
    }

    const QString address = m_queue.takeFirst();
    ++m_scanTicks;

    BtDeviceRecord device;
    if (address == QStringLiteral("58:2F:40:11:22:33")) {
        device = makeDevice(address, QStringLiteral("小米手机"),
                            QStringLiteral("phone"), -52, false, false, 0x5A020C);
    } else if (address == QStringLiteral("7C:9E:BD:44:55:66")) {
        device = makeDevice(address, QStringLiteral("JBL Flip 5"),
                            QStringLiteral("audio-card"), -68, false, false, 0x240404);
    } else if (address == QStringLiteral("A4:C1:38:77:88:99")) {
        // 刻意给 0：真实现里发现回调的 rssi 基本是 0（InterfacesAdded 不带 RSSI），
        // 实时值只能靠 RSSI 更新回调补。界面必须能显示「未知」而不是 0 dBm。
        device = makeDevice(address, QStringLiteral("Mi Band 7"),
                            QStringLiteral("phone"), 0, false, false, 0);
    } else {
        device = makeDevice(address, QString(), QStringLiteral("computer"),
                            -80, false, false, 0);
        // 真实现里没名字的设备根本不会发发现回调（bt_bluez_signals.c:63），
        // 这里模拟成「有地址无名字」让界面的兜底显示也能被验到。
    }

    m_devices.append(device);
    if (m_events.deviceFound)
        m_events.deviceFound(device);

    // 第二台之后补一次 RSSI 更新，验证界面走的是实时值
    if (m_scanTicks >= 2) {
        const QString addr = m_devices.first().address;
        if (m_events.rssiUpdated)
            m_events.rssiUpdated(addr, -48 - m_scanTicks);
    }
}

bool FakeBtBackend::stopScan()
{
    if (!m_scanning)
        return true;
    m_scanning = false;
    m_scanTimer->stop();
    if (m_events.scanState)
        m_events.scanState(BtScanStopped);
    emitLog(QStringLiteral("[模拟] 已停止扫描"), 0);
    return true;
}

bool FakeBtBackend::isScanning() const
{
    return m_scanning;
}

bool FakeBtBackend::pairDevice(const QString &address)
{
    if (!isEnabled()) {
        m_status = QStringLiteral("未上电，无法配对");
        return false;
    }

    m_pendingPair = address;
    if (m_events.bondState)
        m_events.bondState(BtBondBonding, address);
    emitLog(QStringLiteral("[模拟] 开始配对 %1").arg(address), 0);

    // 走一遍确认流程：真机上 agent 回调会把 passkey 抛上来，
    // 界面必须在这条路径上验证「弹确认 → 用户点确定 → 回写」。
    if (m_events.passkeyRequested) {
        // handle 用非零假值；真实现里这是 GDBusMethodInvocation*，
        // 界面只当不透明句柄原样传回，不解释它。
        m_events.passkeyRequested(1, address, 123456, true);
    } else {
        m_pairTimer->start(kPairMs);
    }
    return true;
}

bool FakeBtBackend::unpairDevice(const QString &address)
{
    for (int i = 0; i < m_paired.size(); ++i) {
        if (m_paired.at(i).address.compare(address, Qt::CaseInsensitive) == 0) {
            m_paired.removeAt(i);
            if (m_events.bondState)
                m_events.bondState(BtBondUnbonded, address);
            emitLog(QStringLiteral("[模拟] 已取消配对 %1").arg(address), 0);
            return true;
        }
    }
    m_status = QStringLiteral("设备不在已配对列表中");
    return false;
}

bool FakeBtBackend::connectDevice(const QString &address)
{
    if (!isEnabled()) {
        m_status = QStringLiteral("未上电，无法连接");
        return false;
    }
    for (int i = 0; i < m_paired.size(); ++i) {
        if (m_paired.at(i).address.compare(address, Qt::CaseInsensitive) == 0) {
            m_paired[i].connected = true;
            emitLog(QStringLiteral("[模拟] 已连接 %1").arg(address), 0);
            return true;
        }
    }
    m_status = QStringLiteral("设备未配对，无法连接");
    return false;
}

bool FakeBtBackend::disconnectDevice(const QString &address)
{
    for (int i = 0; i < m_paired.size(); ++i) {
        if (m_paired.at(i).address.compare(address, Qt::CaseInsensitive) == 0) {
            m_paired[i].connected = false;
            emitLog(QStringLiteral("[模拟] 已断开 %1").arg(address), 0);
            return true;
        }
    }
    m_status = QStringLiteral("设备不在已配对列表中");
    return false;
}

QList<BtDeviceRecord> FakeBtBackend::pairedDevices()
{
    return m_paired;
}

void FakeBtBackend::replyPasskey(quintptr handle, bool accept)
{
    if (handle != 1 || m_pendingPair.isEmpty()) {
        emitLog(QStringLiteral("[模拟] 配对确认句柄无效或已用尽"), 2);
        return;
    }

    const QString address = m_pendingPair;
    m_pendingPair.clear();

    if (!accept) {
        if (m_events.bondState)
            m_events.bondState(BtBondBondFailed, address);
        m_status = QStringLiteral("用户拒绝配对");
        emitLog(QStringLiteral("[模拟] 用户拒绝配对 %1").arg(address), 1);
        return;
    }

    // 确认通过：转到「等配对完成」的那个变量上。
    // 不能继续用 m_pendingPair —— 它已经清空，且 onPairTimerFired 会
    // 因为看到空值直接返回，表现为配对永远不完成。
    m_confirmPair = address;
    m_pairTimer->start(kPairMs);
}

void FakeBtBackend::onPairTimerFired()
{
    if (m_confirmPair.isEmpty())
        return;

    const QString address = m_confirmPair;
    m_confirmPair.clear();

    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).address.compare(address, Qt::CaseInsensitive) == 0) {
            BtDeviceRecord device = m_devices.at(i);
            device.paired = true;
            m_paired.append(device);
            break;
        }
    }

    if (m_events.bondState)
        m_events.bondState(BtBondBonded, address);
    emitLog(QStringLiteral("[模拟] 配对成功 %1").arg(address), 0);
}

QString FakeBtBackend::localAddress()
{
    return QStringLiteral("11:22:33:44:55:66");
}

void FakeBtBackend::shutdown()
{
    m_scanTimer->stop();
    m_enableTimer->stop();
    m_pairTimer->stop();
    m_scanning = false;
    m_pendingEnable = false;
    m_initialized = false;
}

QString FakeBtBackend::backendName() const
{
    return QStringLiteral("fake");
}

QString FakeBtBackend::statusText() const
{
    return m_status;
}

BtDeviceRecord FakeBtBackend::makeDevice(const QString &address, const QString &name,
                                         const QString &icon, int rssi, bool paired,
                                         bool connected, quint32 deviceClass) const
{
    BtDeviceRecord device;
    device.address = address;
    device.name = name;
    device.icon = icon;
    device.rssi = rssi;
    device.paired = paired;
    device.connected = connected;
    device.deviceClass = deviceClass;
    return device;
}
