#ifndef HMI_SERVICES_BTWORKER_H
#define HMI_SERVICES_BTWORKER_H

#include <QList>
#include <QObject>
#include <QString>

#include "btdevice.h"

class IBtBackend;

// 工作线程一侧的宿主：本类的所有槽都跑在 BtService 建的那条 QThread 上，
// 因此这里的方法可以放心调用 libbtmg 的阻塞 API。
//
// 为什么单独一个类，而不是直接在 BtService 里用 QThread + 信号：
//   · libbtmg 要求 enable/deinit 等全部在**同一条**线程上调用，
//     散在 BtService 里容易出现「这条走了主线程」的失误；
//   · 后端实例的创建与销毁都发生在这条线程，生命周期边界清楚；
//   · 换成假后端时上层零改动（工厂在 BtService::startWorker 里只调一次）。
//
// 事件方向：
//   回调（btmanager 线程）→ 本类信号 → 队列投递 → BtService（GUI 线程）
// 从非 Qt 创建的线程 emit 信号是安全的：Qt 的 AutoConnection 会按
// 「发送线程 != 接收对象所在线程」判成 QueuedConnection，
// postEvent 本身是线程安全的。前提是参数类型已 qRegisterMetaType。
class BtWorker : public QObject
{
    Q_OBJECT
public:
    // simulated 在建对象时定下来：后端实例在 initialize() 里才创建（那条线程上），
    // 但「用哪个后端」是启动参数，不该等到线程里再问配置。
    explicit BtWorker(bool simulated, QObject *parent = nullptr);
    ~BtWorker() override;

public slots:
    void initialize(int profileMask);
    void setEnabled(bool enabled);
    void startScan(int scanType);
    void stopScan();
    void pair(const QString &address);
    void unpair(const QString &address);
    void connectDevice(const QString &address);
    void disconnectDevice(const QString &address);
    void reloadPaired();
    void replyPasskey(quintptr handle, bool accept);
    void shutdown();

signals:
    void adapterState(int state);
    void scanState(int state);
    void deviceFound(const BtDeviceRecord &device);
    void deviceRemoved(const QString &address);
    void rssiUpdated(const QString &address, int rssi);
    void bondState(int state, const QString &address);
    void passkeyRequested(quintptr handle, const QString &address,
                          unsigned int passkey, bool numericOnly);
    void log(const QString &text, int level);
    void pairedList(const QList<BtDeviceRecord> &devices);
    void localAddressResolved(const QString &address);

private:
    void ensureBackend();
    void emitLog(const QString &text, int level);

    IBtBackend *m_backend;
    bool        m_shutdown;
    bool        m_simulatedRequested;
};

#endif // HMI_SERVICES_BTWORKER_H
