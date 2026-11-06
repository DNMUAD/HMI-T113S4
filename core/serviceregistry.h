#ifndef HMI_CORE_SERVICEREGISTRY_H
#define HMI_CORE_SERVICEREGISTRY_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "ideviceservice.h"

class QTimer;

// 设计模式 #9：注册表 / 服务定位器（Registry + Service Locator）
//
// 服务自己在启动时登记「设备 id → 构造工厂」，注册表负责：
//  · 首次访问时实例化（懒加载，避免启动期把不可用的硬件全部点一遍）；
//  · 按各自声明的 pollIntervalMs() 调度周期探测 —— WiFi 5s、蓝牙 15s、总线类不轮询；
//  · 给「全部刷新」提供一个入口，上层不必枚举具体服务类。
//
// 页面仍然可以用 WifiService::instance() 拿强类型指针；只有框架层用注册表。
class ServiceRegistry : public QObject
{
    Q_OBJECT
public:
    typedef std::function<IDeviceService *()> ServiceFactory;

    static ServiceRegistry *instance();

    void addFactory(const QString &deviceId, ServiceFactory factory);
    void track(IDeviceService *service);            // 服务构造时自动登记

    QList<IDeviceService *> services() const;
    IDeviceService *service(const QString &deviceId) const;
    QStringList deviceIds() const;

    void start(int tickMs = 1000);                  // 启动周期探测调度器
    void stop();
    void refreshAll();                              // 立即全部探测一次

signals:
    void serviceAdded(const QString &deviceId);

private slots:
    void onTick();

private:
    explicit ServiceRegistry(QObject *parent = nullptr);

    QList<IDeviceService *>        m_services;
    QHash<QString, ServiceFactory> m_factories;
    QHash<IDeviceService *, int>   m_countdown;
    QTimer                        *m_timer;
    int                            m_tickMs;
};

// 服务实现文件末尾调用一次即可完成自注册（只在静态初始化期登记工厂，不建对象）。
#define HMI_REGISTER_SERVICE(DEVICE_ID, SERVICE_CLASS)                                    \
    namespace {                                                                            \
    struct SERVICE_CLASS##ServiceRegistrar                                                 \
    {                                                                                      \
        SERVICE_CLASS##ServiceRegistrar()                                                  \
        {                                                                                  \
            ServiceRegistry::instance()->addFactory(                                       \
                QStringLiteral(DEVICE_ID),                                                 \
                []() -> IDeviceService * { return SERVICE_CLASS::instance(); });           \
        }                                                                                  \
    };                                                                                     \
    static SERVICE_CLASS##ServiceRegistrar g_serviceRegistrar_##SERVICE_CLASS;             \
    }

#endif // HMI_CORE_SERVICEREGISTRY_H
