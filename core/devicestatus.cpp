#include "devicestatus.h"

#include <QObject>

QString deviceStateName(DeviceState state)
{
    switch (state) {
    case DeviceOff:   return QObject::tr("关闭");
    case DeviceBusy:  return QObject::tr("处理中");
    case DeviceReady: return QObject::tr("就绪");
    case DeviceError: return QObject::tr("错误");
    case DeviceUnknown:
    default:          return QObject::tr("未知");
    }
}

DeviceStatus::DeviceStatus()
    : state(DeviceUnknown)
{
}

DeviceStatus::DeviceStatus(const QString &deviceId, DeviceState deviceState, const QString &text)
    : device(deviceId)
    , state(deviceState)
    , detail(text)
{
}

bool DeviceStatus::isOnline() const
{
    return state == DeviceReady;
}

bool DeviceStatus::operator==(const DeviceStatus &other) const
{
    return device == other.device && state == other.state && detail == other.detail;
}

bool DeviceStatus::operator!=(const DeviceStatus &other) const
{
    return !(*this == other);
}
