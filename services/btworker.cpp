#include "btworker.h"

#include "ibtbackend.h"

BtWorker::BtWorker(bool simulated, QObject *parent)
    : QObject(parent)
    , m_backend(nullptr)
    , m_shutdown(false)
    , m_simulatedRequested(simulated)
{
}

BtWorker::~BtWorker()
{
    // 后端在创建它的线程里销毁（本来就是这条线程）
    if (m_backend) {
        m_backend->shutdown();
        delete m_backend;
        m_backend = nullptr;
    }
}

void BtWorker::emitLog(const QString &text, int level)
{
    emit log(text, level);
}

void BtWorker::ensureBackend()
{
    if (m_backend)
        return;

    // 工厂只在这里调一次：真机走 BtmgBackend，桌面/--simulate 走 FakeBtBackend。
    // 是否模拟由配置决定，因此通过 IBtBackend::createBtBackend 的入参传入。
    m_backend = createBtBackend(m_simulatedRequested);
}

void BtWorker::initialize(int profileMask)
{
    ensureBackend();
    if (!m_backend)
        return;

    IBtBackend::Events events;
    events.adapterState = [this](int state) { emit adapterState(state); };
    events.scanState = [this](int state) { emit scanState(state); };
    events.deviceFound = [this](const BtDeviceRecord &device) { emit deviceFound(device); };
    events.deviceRemoved = [this](const QString &address) { emit deviceRemoved(address); };
    events.rssiUpdated = [this](const QString &address, int rssi) { emit rssiUpdated(address, rssi); };
    events.bondState = [this](int state, const QString &address) { emit bondState(state, address); };
    events.passkeyRequested = [this](quintptr handle, const QString &address,
                                     unsigned int passkey, bool numericOnly) {
        emit passkeyRequested(handle, address, passkey, numericOnly);
    };
    events.log = [this](const QString &text, int level) { emitLog(text, level); };
    m_backend->setEvents(events);

    emitLog(QStringLiteral("蓝牙后端：%1").arg(m_backend->backendName()), 0);

    if (!m_backend->initialize(profileMask)) {
        emitLog(QStringLiteral("初始化失败：%1").arg(m_backend->statusText()), 2);
        return;
    }
    emitLog(QStringLiteral("初始化完成：%1").arg(m_backend->statusText()), 0);
}

void BtWorker::setEnabled(bool enabled)
{
    if (!m_backend)
        return;

    emitLog(enabled ? QStringLiteral("开始上电（真机实测约 4 秒）")
                    : QStringLiteral("开始下电"), 0);

    if (!m_backend->setEnabled(enabled))
        emitLog(QStringLiteral("%1：%2").arg(enabled ? QStringLiteral("上电失败")
                                                    : QStringLiteral("下电失败"),
                                            m_backend->statusText()), 2);

    // 上电后补一次本地地址与已配对列表
    if (enabled && m_backend->isEnabled()) {
        const QString address = m_backend->localAddress();
        if (!address.isEmpty())
            emit localAddressResolved(address);
        reloadPaired();
    }
}

void BtWorker::startScan(int scanType)
{
    if (!m_backend)
        return;
    if (!m_backend->startScan(scanType))
        emitLog(QStringLiteral("扫描失败：%1").arg(m_backend->statusText()), 1);
}

void BtWorker::stopScan()
{
    if (!m_backend)
        return;
    if (!m_backend->stopScan())
        emitLog(QStringLiteral("停止扫描失败：%1").arg(m_backend->statusText()), 1);
}

void BtWorker::pair(const QString &address)
{
    if (!m_backend)
        return;
    if (!m_backend->pairDevice(address))
        emitLog(QStringLiteral("配对失败：%1").arg(m_backend->statusText()), 1);
}

void BtWorker::unpair(const QString &address)
{
    if (!m_backend)
        return;
    if (!m_backend->unpairDevice(address))
        emitLog(QStringLiteral("取消配对失败：%1").arg(m_backend->statusText()), 1);
    reloadPaired();
}

void BtWorker::connectDevice(const QString &address)
{
    if (!m_backend)
        return;
    // 最慢的一条：D-Bus Connect 超时 15000ms（bt_device.c:123）。
    // 在这条线程上阻塞没关系，GUI 线程只等异步信号。
    if (!m_backend->connectDevice(address))
        emitLog(QStringLiteral("连接失败：%1").arg(m_backend->statusText()), 1);
    reloadPaired();
}

void BtWorker::disconnectDevice(const QString &address)
{
    if (!m_backend)
        return;
    if (!m_backend->disconnectDevice(address))
        emitLog(QStringLiteral("断开失败：%1").arg(m_backend->statusText()), 1);
    reloadPaired();
}

void BtWorker::reloadPaired()
{
    if (!m_backend)
        return;
    emit pairedList(m_backend->pairedDevices());
}

void BtWorker::replyPasskey(quintptr handle, bool accept)
{
    if (!m_backend)
        return;
    m_backend->replyPasskey(handle, accept);
}

void BtWorker::shutdown()
{
    if (m_shutdown)
        return;
    m_shutdown = true;

    if (m_backend) {
        m_backend->shutdown();
        delete m_backend;
        m_backend = nullptr;
    }
}
