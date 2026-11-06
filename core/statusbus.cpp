#include "statusbus.h"
#include "logpanel.h"
#include "singleton.h"

StatusBus *StatusBus::instance()
{
    return hmi::singleton<StatusBus>([]() { return new StatusBus; });
}

StatusBus::StatusBus(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DeviceStatus>("DeviceStatus");
}

void StatusBus::publish(const DeviceStatus &status)
{
    if (status.device.isEmpty())
        return;

    const bool known = m_status.contains(status.device);
    if (known) {
        const DeviceStatus prev = m_status.value(status.device);
        if (prev.state == status.state && prev.detail == status.detail)
            return;                       // 完全重复，静默丢弃（避免状态栏闪烁 / 日志刷屏）
    }

    StateTransitionTable &table = m_tables[status.device];
    if (!table.transit(status.state)) {
        LogPanel::post(QStringLiteral("忽略非法状态迁移 %1: %2 → %3")
                       .arg(status.device, deviceStateName(table.current()),
                            deviceStateName(status.state)),
                       LogPanel::Warn);
        return;
    }

    m_status.insert(status.device, status);
    emit changed(status);
}

DeviceStatus StatusBus::status(const QString &device) const
{
    return m_status.value(device, DeviceStatus(device, DeviceUnknown));
}

QList<DeviceStatus> StatusBus::snapshot() const
{
    return m_status.values();
}

void StatusBus::forget(const QString &device)
{
    m_status.remove(device);
    m_tables.remove(device);
}
