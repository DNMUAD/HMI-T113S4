#ifndef HMI_CORE_DEVICESERVICE_H
#define HMI_CORE_DEVICESERVICE_H

#include <QObject>
#include <QString>

#include "devicestatus.h"
#include "ideviceservice.h"

// 设计模式：模板方法（Template Method）在服务层的应用
//
// 硬件服务有完全相同的骨架：拿 id/名字/轮询周期 → 探测 → 发布状态。
// 基类把骨架实现好（登记注册表、发布到 StatusBus、统一 deviceId 门面），
// 子类只实现自己的 refresh() 与业务动作（connectTo / dhcp / start ...）。
class DeviceService : public QObject, public IDeviceService
{
    Q_OBJECT
public:
    QString deviceId() const override;
    QString displayName() const override;
    int  pollIntervalMs() const override;
    void refresh() override;                       // 默认空实现，子类按需覆盖

signals:
    void statusChanged(const DeviceStatus &status);

protected:
    DeviceService(const QString &deviceId, const QString &displayName,
                  int pollIntervalMs = 0, QObject *parent = nullptr);

    void setPollIntervalMs(int ms);
    void publish(DeviceState state, const QString &detail = QString());

private:
    QString m_deviceId;
    QString m_displayName;
    int     m_pollIntervalMs;
};

#endif // HMI_CORE_DEVICESERVICE_H
