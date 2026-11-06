#ifndef HMI_CORE_DEVICESTATUS_H
#define HMI_CORE_DEVICESTATUS_H

#include <QMetaType>
#include <QString>

// 设计模式：观察者（Observer）里流动的「事件对象」
//
// 状态栏、页面、日志都只关心「某设备现在什么状态」，所以统一成这一个值对象，
// 服务层 publish 一次，任意订阅者都能收到，彼此不知道对方存在。
enum DeviceState
{
    DeviceUnknown = 0,   // 尚未探测
    DeviceOff,           // 明确不可用 / 未连接
    DeviceBusy,          // 正在执行动作（连接中、打开中）
    DeviceReady,         // 就绪（已连上、已打开、运行中）
    DeviceError          // 出错（命令失败、设备缺失）
};

QString deviceStateName(DeviceState state);

struct DeviceStatus
{
    QString     device;     // 与 IDeviceService::deviceId() 一致："wifi" "eth" "adb" ...
    DeviceState state;
    QString     detail;     // IP / 设备节点 / 错误原因，供状态栏直接显示

    DeviceStatus();
    DeviceStatus(const QString &deviceId, DeviceState deviceState,
                 const QString &text = QString());

    bool isOnline() const;
    bool operator==(const DeviceStatus &other) const;
    bool operator!=(const DeviceStatus &other) const;
};

Q_DECLARE_METATYPE(DeviceStatus)

#endif // HMI_CORE_DEVICESTATUS_H
