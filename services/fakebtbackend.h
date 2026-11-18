#ifndef HMI_SERVICES_FAKEBTBACKEND_H
#define HMI_SERVICES_FAKEBTBACKEND_H

#include <QList>
#include <QObject>
#include <QString>

#include "btdevice.h"
#include "ibtbackend.h"

class QTimer;

// 设计模式 #17：空对象（Null Object）—— 蓝牙版 SimulatedCommandRunner
//
// 桌面 x86 没有 libbtmg，也没装 BlueZ 后端插件（Qt 5.15 的 bluez 后端是
// 插件加载，rootfs 和目标机都缺 usr/lib/qt/plugins/bluetooth/），
// 所以界面必须有另一条路可以跑起来。
//
// 本类用 QTimer 异步返回，模拟真实现的时序特征，而不是立即返回：
//   · 上电要等一会儿才变 On（真机 3.7s，这里 1.5s，够看清「处理中」）
//   · 扫描是「陆续发现设备」，不是一次性给全表
//   · 有一个设备走配对确认流程（带 passkey），用来验证 UI 的确认面板
//   · 有一个设备没有名字（真实现里这种设备根本不会发发现回调，
//     这里刻意放进已配对列表，用来验证界面的空名兜底）
//
// 时序、状态迁移、空名兜底这些逻辑都在桌面先验完，上板只需换实现。
class FakeBtBackend : public QObject, public IBtBackend
{
    Q_OBJECT

public:
    explicit FakeBtBackend(QObject *parent = nullptr);
    ~FakeBtBackend() override;

    void setEvents(const Events &events) override;

    bool initialize(int profileMask) override;
    bool setEnabled(bool enabled) override;
    bool isEnabled() const override;

    bool startScan(int scanType) override;
    bool stopScan() override;
    bool isScanning() const override;

    bool pairDevice(const QString &address) override;
    bool unpairDevice(const QString &address) override;
    bool connectDevice(const QString &address) override;
    bool disconnectDevice(const QString &address) override;

    QList<BtDeviceRecord> pairedDevices() override;

    void replyPasskey(quintptr handle, bool accept) override;

    QString localAddress() override;
    void shutdown() override;
    QString backendName() const override;
    QString statusText() const override;

private slots:
    void onEnableDone();
    void onScanTick();
    void onPairTimerFired();

private:
    void emitLog(const QString &text, int level);
    BtDeviceRecord makeDevice(const QString &address, const QString &name,
                              const QString &icon, int rssi, bool paired,
                              bool connected, quint32 deviceClass) const;

    Events        m_events;
    QTimer       *m_enableTimer;
    QTimer       *m_scanTimer;
    QTimer       *m_pairTimer;
    QList<BtDeviceRecord> m_devices;      // 已「发现」的设备
    QList<BtDeviceRecord> m_paired;       // 已配对
    QString       m_pendingPair;      // 等用户确认 passkey 的那台
    QString       m_confirmPair;      // 已确认、等配对完成的那台
    QStringList   m_queue;                // 还没被「发现」的设备地址队列
    QString       m_status;
    bool          m_initialized;
    bool          m_enabled;
    bool          m_pendingEnable;
    bool          m_scanning;
    int           m_scanTicks;
    int           m_profileMask;
};

#endif // HMI_SERVICES_FAKEBTBACKEND_H
