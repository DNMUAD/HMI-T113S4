#ifndef HMI_CORE_IDEVICESERVICE_H
#define HMI_CORE_IDEVICESERVICE_H

#include <QString>

// 设计模式：依赖倒置（DIP）用的抽象接口 / 接口隔离（ISP）
//
// 上层的「状态栏轮询」「自检」只依赖这四个方法，不依赖 WiFi / 以太网 / ADB 的具体类。
// 于是新增一个设备服务 = 实现这个接口 + 注册一次，其余代码零改动。

class IDeviceService
{
public:
    virtual ~IDeviceService() { }

    virtual QString deviceId() const = 0;       // 与 StatusBus 的 device 键一致
    virtual QString displayName() const = 0;
    virtual int  pollIntervalMs() const = 0;    // 0 = 不参与周期轮询
    virtual void refresh() = 0;                 // 主动探测一次
};

#endif // HMI_CORE_IDEVICESERVICE_H
