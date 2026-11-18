#ifndef BTSERVICE_H
#define BTSERVICE_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include "btdevice.h"
#include "deviceservice.h"

class BtWorker;
class IBtBackend;
class QThread;
class QTimer;

// 蓝牙服务：界面侧唯一入口。
//
// 与 WiFi/ETH 那些服务最大的不同：**本类的方法一律不阻塞**，
// 真正干活的 libbtmg 全部在一个专用工作线程上（BtWorker + IBtBackend）。
// 原因见 services/ibtbackend.h 的文件头：libbtmg 每个 API 都是
// act_transfer 同步投递 + 无限等待，上电实测 3.7 秒、连接最长 15 秒，
// 放进 GUI 线程就是整窗卡死。
//
// 因此 refresh() 的语义也变了：它不再「现场查一次」，
// 只把工作线程上报的状态摊出来。ServiceRegistry 每 15s 在 GUI 线程调它，
// 一个字都不会碰到库。
//
// ── 三个自己维护的状态位 ────────────────────────────────────────
// 库本身不提供可靠状态查询，原因有三（均已在 SDK 源码核对）：
//
//   · bt_manager.c 里 55 处 `if (check_bt_is_on == false)` 全部失效——
//     check_bt_is_on 是函数不是宏（bt_manager.c:239），与 false 比较恒假。
//     没上电就调 start_scan 会真的执行下去，拿到 BT_OK 却什么也没发生
//     （g_dbus_call_sync_check_error(NULL) 直接 return BT_OK，common.c:110）。
//   · get_adapter_state() 未上电时把错误码 -2 强转成枚举返回，语义不可靠。
//   · 配对成功的 BONDED 回调经常永远不来（bt_bluez_signals.c:133 的
//     r_class == 0 提前 return；bt_device_pair_callback 成功路径不发回调）。
//
// 所以 adapterOn / scanning / bonding 三个状态都在这里维护，
// 界面只信这三个，不信任何「查询接口」。
class BtService : public DeviceService
{
    Q_OBJECT
public:
    static BtService *instance();

    void refresh() override;            // 只摊状态，不查库（见类注释）

    void powerOn();
    void powerOff();
    void scan();                        // 开始扫描（先清列表）
    void stopScan();
    void pair(const QString &address);
    void unpair(const QString &address);
    void connectDevice(const QString &address);
    void disconnectDevice(const QString &address);

    // 配对确认：把工作线程带出来的 handle 交回库。
    // 必须在 20s 内调用，否则 BlueZ 侧超时（对端约 25s）。
    void confirmPairing(quintptr handle, bool accept);

    // 已配对设备重新拉一次（工作线程上执行，异步回填）
    void reloadPaired();

    bool adapterOn() const { return m_adapterOn; }
    bool scanning() const { return m_scanning; }
    bool busy() const { return m_busy; }
    QString localAddress() const { return m_localAddress; }
    QString backendName() const;

signals:
    // 扫描结果事件（工作线程 → GUI，跨线程自动排到队列）
    void deviceFound(const BtDeviceRecord &device);
    void deviceRemoved(const QString &address);
    void rssiUpdated(const QString &address, int rssi);
    void scanStateChanged(int state);

    // 配对
    void bondStateChanged(int state, const QString &address);
    void passkeyRequested(quintptr handle, const QString &address,
                          unsigned int passkey, bool numericOnly);

    void pairedListChanged(const QList<BtDeviceRecord> &devices);
    void adapterStateChanged(bool on);

    // 内部：GUI 线程 → 工作线程的请求
    void requestInitialize(int profileMask);
    void requestEnable(bool enabled);
    void requestScan(int scanType);
    void requestStopScan();
    void requestPair(const QString &address);
    void requestUnpair(const QString &address);
    void requestConnect(const QString &address);
    void requestDisconnect(const QString &address);
    void requestReloadPaired();
    void requestReplyPasskey(quintptr handle, bool accept);
    void requestShutdown();

private slots:
    void onAdapterState(int state);
    void onScanState(int state);
    void onDeviceFound(const BtDeviceRecord &device);
    void onDeviceRemoved(const QString &address);
    void onRssiUpdated(const QString &address, int rssi);
    void onBondState(int state, const QString &address);
    void onPasskeyRequested(quintptr handle, const QString &address,
                            unsigned int passkey, bool numericOnly);
    void onPairedList(const QList<BtDeviceRecord> &devices);
    void onBackendLog(const QString &text, int level);
    void onBondWatchdog();

private:
    explicit BtService(QObject *parent = nullptr);
    ~BtService() override;

    void startWorker();

    QThread      *m_thread;
    BtWorker     *m_worker;
    QTimer       *m_bondWatchdog;
    QString       m_localAddress;
    QString       m_pendingPairAddress;
    bool          m_adapterOn;
    bool          m_scanning;
    bool          m_busy;
    bool          m_initialized;
};

#endif // BTSERVICE_H
