#ifndef HMI_SERVICES_BTMGBACKEND_H
#define HMI_SERVICES_BTMGBACKEND_H

#include <QByteArray>
#include <QList>
#include <QString>

#include "btdevice.h"
#include "ibtbackend.h"

// 直接包含厂商头，而不是前置声明。
//
// 原因：bt_manager.h 里 btmg_bt_device_t 是**匿名 struct 的 typedef**
// （`typedef struct { ... } btmg_bt_device_t;`），没有 struct tag，
// 写 `struct btmg_bt_device_t;` 会造出一个同名但无关的类型，
// 反过来让 bt_manager.h 报 "conflicting declaration"。
//
// 这不泄漏抽象：本头文件只在 HMI_HAVE_BT 分支被 btmgbackend.cpp 与
// btbackendfactory.cpp 引用，而后者已经用 #ifdef 包住了 include。
// 上层（BtService/BtPage）只认 services/ibtbackend.h。
#include <bt_manager.h>

// btmg_set_log_file_path 声明在 bt_log.h，不在 bt_manager.h。
// 日志默认走 stdout 会被 S70launcher 的重定向吃掉，产品里必须自己设路径。
#include <bt_log.h>

// 板端真实现：直调全志 btmanager（libbtmg.so）。
//
// 只在 ARM 分支（.pro 里 DEFINES += HMI_HAVE_BT）参与编译：
// <bt_manager.h> 与 libbtmg.so 只存在于交叉 sysroot（host/arm-buildroot-linux-gnueabi/
// sysroot/usr/include、usr/lib），x86 桌面 Kit 没有，硬编直接编译不过。
//
// ── 调用线程 ────────────────────────────────────────────────────
// 本类所有方法只允许在 BtWorker 的专用线程调用。libbtmg 每个 API 都是
// act_transfer 同步投递 + 无限等待（api_action.c:50,108）。实测上电 3.67 秒，
// 连接最长 15 秒（D-Bus 超时）。
//
// ── 为什么自己维护 enabled / scanning 状态 ───────────────────────
// bt_manager.c 里 55 处 `if (check_bt_is_on == false) return BT_ERROR_ADATER_NOT_ON;`
// 全部失效：check_bt_is_on 是函数（bt_manager.c:239），函数指针与 false 比较恒假。
// 后果是没上电时调 start_scan 会真走到 bt_adapter_start_scan，
// 此时 bluez_mg.dbus 为 NULL，g_dbus_connection_call_sync 返回 NULL，
// 而 g_dbus_call_sync_check_error(NULL) 直接 return BT_OK（common.c:110）——
// 拿到「成功」但什么也没发生。所以不能依赖库自己的前置检查。
//
// ── 为什么不信 get_adapter_state() ──────────────────────────────
// bt_manager_get_adapter_state() 未上电时把错误码 -2 强转成 btmg_adapter_state_t
// 返回（bt_manager.c:392-402，枚举里没有 -2），语义不可靠。状态由
// adapter_state_cb 维护，查询走缓存。
class BtmgBackend : public IBtBackend
{
public:
    BtmgBackend();
    ~BtmgBackend() override;

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

    // 回调只能拿到 C 侧参数（bt_manager.h 的函数指针不带 user_data），
    // 所以真正的事件处理走一个文件级静态实例指针转发到这里。
    // 声明为 public 是因为转发函数在匿名 namespace 里，无法声明为友元。
    // 这些方法不是给上层用的，调用方只该通过 IBtBackend 接口。
    void handleAdapterState(int state);
    void handleScanState(int state);
    void handleDeviceAdded(const btmg_bt_device_t *device);
    void handleDeviceRemoved(const btmg_bt_device_t *device);
    void handleRssi(const char *address, int rssi);
    void handleBondState(int state, const char *address);
    void handleConfirmPasskey(void *handle, char *device, unsigned int passkey, bool numericOnly);

    // 官方 4.12 节 9 步流程的第 7 步：设置本机蓝牙名（手机端看到的就是它）。
    // 同样是被 handleAdapterState 调用的内部实现，放在这里与上面保持一致。
    void applyAdapterName();

private:
    btmg_callback_t *m_callbacks;
    Events   m_events;
    bool     m_initialized;
    bool     m_enabled;        // 缓存的上电状态，不信库
    bool     m_scanning;       // 缓存的扫描状态
    int      m_profileMask;
    QString  m_status;
    QString  m_localAddress;    // 上电时缓存，避免查询时又走一次同步队列
};

#endif // HMI_SERVICES_BTMGBACKEND_H
