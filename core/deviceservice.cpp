#include "deviceservice.h"
#include "serviceregistry.h"
#include "statusbus.h"

DeviceService::DeviceService(const QString &deviceId, const QString &displayName,
                             int pollIntervalMs, QObject *parent)
    : QObject(parent)
    , m_deviceId(deviceId)
    , m_displayName(displayName)
    , m_pollIntervalMs(pollIntervalMs)
{
    // 自登记：构造即进注册表，之后无论谁先 new 都能被「全部刷新」覆盖到
    ServiceRegistry::instance()->track(this);
}

QString DeviceService::deviceId() const
{
    return m_deviceId;
}

QString DeviceService::displayName() const
{
    return m_displayName;
}

int DeviceService::pollIntervalMs() const
{
    return m_pollIntervalMs;
}

void DeviceService::refresh()
{
}

void DeviceService::setPollIntervalMs(int ms)
{
    m_pollIntervalMs = qMax(0, ms);
}

void DeviceService::publish(DeviceState state, const QString &detail)
{
    const DeviceStatus status(m_deviceId, state, detail);
    StatusBus::instance()->publish(status);      // 观察者总线（含去重与状态校验）
    emit statusChanged(status);
}
