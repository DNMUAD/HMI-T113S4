#ifndef HMI_SERVICES_IBTBACKEND_H
#define HMI_SERVICES_IBTBACKEND_H

#include <QList>
#include <QString>

#include <functional>

#include "btdevice.h"

// 设计模式 #3/#17：策略（Strategy）+ 空对象（Null Object）在蓝牙上的应用
//
// 为什么需要这层抽象：libbtmg 只存在于 ARM 交叉 sysroot（usr/include/bt_manager.h +
// usr/lib/libbtmg.so），x86 桌面 Kit 根本没有这个库。而本工程的做法是「桌面能跑、
// 能截图、能回归」，蓝牙若只有板端能编译，每次改界面都得烧板。
//
// 于是把「谁提供蓝牙能力」抽成接口：
//   BtmgBackend  —— 板端真实现，直调 libbtmg（只在 HMI_HAVE_BT 下编译）
//   FakeBtBackend—— 桌面与 --simulate 的预置数据，行为和真实现同构
//
// 与 core/commandrunner.h + simulatedcommandrunner.* 是同一套思路：
// 上层只看接口，换实现只改工厂里一行。
//
// ── 线程约定（非常重要，违反会冻界面）─────────────────────────────
//
// libbtmg 的每个公开 API 都是同步阻塞的：
//   bt_manager.c:57  act_init(&bt_act_handle, "bt_action", true)  ← 第三个参数 is_sync
//   之后所有调用走 act_transfer，非 action 线程会投队列并在
//   os_net_sem_wait(&handle->sem, WAIT_FOREVER) 上一直等（api_action.c:50,108）
//
// 而 bt_manager_enable(true) 的整条链路里包含：
//   /etc/bluetooth/bt_init.sh start（hciattach 后最多轮询 7s 等 hci0）
//   bt_bluez_init()（每 200ms 轮询等 bluez mainloop 起来）
//   bt_init_profile()（killall 重启 bluealsa 再轮询等进程）
// 实测 3.67 秒（板端 2026-11-18 日志），放进 GUI 线程就是明显卡顿；
// 而 connect 的 15 秒则是整窗卡死。
//
// 所以：本接口的**所有方法只允许在 BtWorker 的专用工作线程上调用**。
// GUI 线程只通过信号槽下发请求、接收事件，一次都不直接碰这里。
//
// 另一个后果：所有 API 共用同一条 action 队列（长度 24，api_action.h:44）。
// connect 的 D-Bus 往返超时是 15000ms（bt_device.c:123），那 15 秒里
// 任何另一个 bt_manager_* 调用都会排在后面等。所以 UI 侧不能依赖同步查询，
// 状态必须靠回调维护（见 BtService 里的注释）。
class IBtBackend
{
public:
    // 库把事件推回应用的方式。全部在 btmanager 自己的线程上触发：
    //   adapterState —— action 线程（bt_action.c）
    //   deviceFound / deviceRemoved / rssiUpdated / bondState / passkeyRequested
    //                —— bluez GMainLoop 线程（bt_bluez.c:138 bt_routine）
    // 实现方不要在回调里做耗时操作，更不要阻塞等 UI。
    struct Events
    {
        std::function<void(int adapterState)>    adapterState;
        std::function<void(int scanState)>       scanState;
        std::function<void(const BtDeviceRecord &)> deviceFound;
        std::function<void(const QString &address)> deviceRemoved;
        std::function<void(const QString &address, int rssi)> rssiUpdated;
        std::function<void(int bondState, const QString &address)> bondState;

        // 配对确认请求。handle 是 BlueZ 的 GDBusMethodInvocation*，
        // 必须由 replyPasskey() 原样交回，否则对端约 25s 超时失败。
        // 回调里绝不能阻塞等用户点确认 —— 会让整条 bluez mainloop 停住。
        std::function<void(quintptr handle, const QString &address,
                           unsigned int passkey, bool numericOnly)> passkeyRequested;

        std::function<void(const QString &text, int level)> log;
    };

    virtual ~IBtBackend() { }

    virtual void setEvents(const Events &events) = 0;

    // 只做 preinit / profile / 回调注册 / init，不碰硬件，不拉起 hci0。
    // 返回 false 时后续所有调用都不可用（见 statusText()）。
    virtual bool initialize(int profileMask) = 0;

    // 上电 / 下电。阻塞，实测约 3.7 秒（见文件头注释）。
    // 失败时 btmanager 内部会调用 bt_init.sh stop，把整条栈拆掉，
    // 所以不要做成「失败立即重试」。
    virtual bool setEnabled(bool enabled) = 0;

    // 缓存的上电状态。真实现不能回头去问库（见 BtService 里关于
    // check_bt_is_on 失效的说明），只能自己维护。
    virtual bool isEnabled() const = 0;

    // 扫描。注意真实现的 startScan() 第一句是 bt_remove_unpaired_devices()，
    // 会把已发现但未配对的设备从 BlueZ 缓存里删掉，所以每次开始扫描
    // 都要把本地列表清空重来，不能指望它累加。
    virtual bool startScan(int scanType) = 0;
    virtual bool stopScan() = 0;
    virtual bool isScanning() const = 0;

    virtual bool pairDevice(const QString &address) = 0;
    virtual bool unpairDevice(const QString &address) = 0;
    virtual bool connectDevice(const QString &address) = 0;
    virtual bool disconnectDevice(const QString &address) = 0;

    // 已配对列表。真实现内部会 btmg_bt_device_t 分配内存，调用方拿到的是
    // 深拷贝后的 QList，库内存在返回前已释放。
    virtual QList<BtDeviceRecord> pairedDevices() = 0;

    // 回复配准确认。handle 只能用一次。
    virtual void replyPasskey(quintptr handle, bool accept) = 0;

    virtual QString localAddress() = 0;

    // 停止并释放。调用后本对象不可再用。
    virtual void shutdown() = 0;

    virtual QString backendName() const = 0;

    // 最近一次失败的原因，供 UI 直接显示（不必再解析错误码）
    virtual QString statusText() const = 0;
};

// 工厂：按编译目标返回真实现或假实现。
// 板端（HMI_HAVE_BT）在非模拟模式下返回 BtmgBackend，其余一律 FakeBtBackend。
IBtBackend *createBtBackend(bool simulated);

// 本编译单元是否带 libbtmg（供 UI 显示「桌面不支持」之类的提示）
bool btBackendCompiledIn();

#endif // HMI_SERVICES_IBTBACKEND_H
