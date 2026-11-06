#ifndef HMI_CORE_STATUSBUS_H
#define HMI_CORE_STATUSBUS_H

#include <QHash>
#include <QList>
#include <QObject>

#include "devicestatus.h"
#include "statetable.h"

// 设计模式 #8：观察者（Observer）+ 中介者（Mediator）
//
// 设备状态的总线：服务层 publish，状态栏 / 页面 / 日志 subscribe。
//  · Observer：一对多通知，订阅者互相不知道；
//  · Mediator：WiFi 服务不需要认识 MainWindow，MainWindow 也不需要认识 WiFi 服务，
//    两者只依赖 StatusBus，新增设备类型不必改状态栏代码。
//  · 内部再接一层状态转移表（State），把重复上报与非法跳变挡在广播之前。
class StatusBus : public QObject
{
    Q_OBJECT
public:
    static StatusBus *instance();

    void publish(const DeviceStatus &status);
    DeviceStatus status(const QString &device) const;
    QList<DeviceStatus> snapshot() const;
    void forget(const QString &device);

signals:
    void changed(const DeviceStatus &status);

private:
    explicit StatusBus(QObject *parent = nullptr);

    QHash<QString, DeviceStatus>        m_status;
    QHash<QString, StateTransitionTable> m_tables;
};

#endif // HMI_CORE_STATUSBUS_H
