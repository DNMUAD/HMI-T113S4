#include "serviceregistry.h"
#include "logpanel.h"
#include "singleton.h"

#include <QTimer>

ServiceRegistry *ServiceRegistry::instance()
{
    return hmi::singleton<ServiceRegistry>([]() { return new ServiceRegistry; });
}

ServiceRegistry::ServiceRegistry(QObject *parent)
    : QObject(parent)
    , m_timer(nullptr)
    , m_tickMs(1000)
{
}

void ServiceRegistry::addFactory(const QString &deviceId, ServiceFactory factory)
{
    if (deviceId.isEmpty() || !factory)
        return;
    m_factories.insert(deviceId, factory);
}

void ServiceRegistry::track(IDeviceService *service)
{
    if (!service || m_services.contains(service))
        return;
    m_services.append(service);
    m_countdown.insert(service, 0);
    emit serviceAdded(service->deviceId());
}

QList<IDeviceService *> ServiceRegistry::services() const
{
    return m_services;
}

IDeviceService *ServiceRegistry::service(const QString &deviceId) const
{
    for (int i = 0; i < m_services.size(); ++i) {
        if (m_services.at(i)->deviceId() == deviceId)
            return m_services.at(i);
    }
    return nullptr;
}

QStringList ServiceRegistry::deviceIds() const
{
    QStringList ids;
    for (int i = 0; i < m_services.size(); ++i)
        ids << m_services.at(i)->deviceId();
    return ids;
}

void ServiceRegistry::start(int tickMs)
{
    m_tickMs = qBound(200, tickMs, 5000);

    // 懒加载：到这一步才真正实例化各服务（此时 QApplication 已就绪）
    QHash<QString, ServiceFactory>::ConstIterator it = m_factories.constBegin();
    for (; it != m_factories.constEnd(); ++it) {
        IDeviceService *svc = it.value()();
        if (svc)
            track(svc);
    }

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(m_tickMs);
        connect(m_timer, &QTimer::timeout, this, &ServiceRegistry::onTick);
    }
    m_timer->start();

    LogPanel::post(QStringLiteral("服务注册表就绪：%1（轮询基准 %2ms）")
                   .arg(deviceIds().join(QStringLiteral(", "))).arg(m_tickMs),
                   LogPanel::Debug);
}

void ServiceRegistry::stop()
{
    if (m_timer)
        m_timer->stop();
}

void ServiceRegistry::refreshAll()
{
    for (int i = 0; i < m_services.size(); ++i) {
        IDeviceService *svc = m_services.at(i);
        m_countdown.insert(svc, svc->pollIntervalMs());
        svc->refresh();
    }
}

void ServiceRegistry::onTick()
{
    for (int i = 0; i < m_services.size(); ++i) {
        IDeviceService *svc = m_services.at(i);
        const int interval = svc->pollIntervalMs();
        if (interval <= 0)
            continue;                       // 总线 / 播放器类不参与周期探测

        int left = m_countdown.value(svc, 0) - m_tickMs;
        if (left > 0) {
            m_countdown.insert(svc, left);
            continue;
        }
        m_countdown.insert(svc, interval);
        svc->refresh();
    }
}
