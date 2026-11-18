#include "btmgbackend.h"

// <bt_manager.h> 已由头文件引入（原因见那里：btmg_bt_device_t 是匿名 typedef，
// 不能前置声明）。这里不再重复。

namespace {

// bt_manager.h 的回调是裸 C 函数指针，没有 user_data 参数，
// 所以只能靠一个文件级静态实例把事件送回对象。
// 本工程蓝牙后端全局唯一（BtService 单例持有），这样够用。
BtmgBackend *g_backend = nullptr;

BtDeviceRecord toRecord(const btmg_bt_device_t *src)
{
    BtDeviceRecord record;
    if (!src)
        return record;

    // 必须当场深拷贝：remote_address / remote_name / icon 这三个指针
    // 在回调返回后立刻被 bt_free_device 释放（bt_bluez_signals.c:89-92），
    // 留到回调外面就是悬空指针。
    if (src->remote_address)
        record.address = QString::fromLatin1(src->remote_address);
    if (src->remote_name)
        record.name = QString::fromUtf8(src->remote_name);
    if (src->icon)
        record.icon = QString::fromLatin1(src->icon);
    record.rssi = src->rssi;
    record.paired = src->paired;
    record.connected = src->connected;
    record.deviceClass = src->r_class;
    return record;
}

// ── 静态回调 ────────────────────────────────────────────────────
// 全部在 btmanager 自己的线程上触发：
//   onAdapterState —— action 线程
//   其余           —— bluez GMainLoop 线程（bt_bluez.c:138 bt_routine）
// 原则：只做深拷贝 + 转发，绝不阻塞、绝不碰 Qt 控件。

void onAdapterStateCb(btmg_adapter_state_t state)
{
    if (g_backend)
        g_backend->handleAdapterState(int(state));
}

void onScanStatusCb(btmg_scan_state_t status)
{
    if (g_backend)
        g_backend->handleScanState(int(status));
}

void onDeviceAddCb(btmg_bt_device_t *device)
{
    if (g_backend)
        g_backend->handleDeviceAdded(device);
}

void onDeviceRemoveCb(btmg_bt_device_t *device)
{
    if (g_backend)
        g_backend->handleDeviceRemoved(device);
}

void onUpdateRssiCb(const char *address, int rssi)
{
    if (g_backend)
        g_backend->handleRssi(address, rssi);
}

void onBondStateCb(btmg_bond_state_t state, const char *bd_addr)
{
    if (g_backend)
        g_backend->handleBondState(int(state), bd_addr);
}

void onConfirmPasskeyCb(void *handle, char *device, unsigned int passkey)
{
    if (g_backend)
        g_backend->handleConfirmPasskey(handle, device, passkey, true);
}

void onRequestAuthorizeCb(void *handle, char *device)
{
    // 授权类请求没有 passkey，numericOnly 传 false 让界面只显示「允许/拒绝」
    if (g_backend)
        g_backend->handleConfirmPasskey(handle, device, 0, false);
}

} // namespace

BtmgBackend::BtmgBackend()
    : m_callbacks(nullptr)
    , m_initialized(false)
    , m_enabled(false)
    , m_scanning(false)
    , m_profileMask(0)
{
    g_backend = this;
}

BtmgBackend::~BtmgBackend()
{
    if (g_backend == this)
        g_backend = nullptr;
    // m_callbacks 由 bt_manager_preinit 用 malloc 分配，
    // bt_manager_deinit 内部 free 并置空（bt_manager.c:132-140）。
    // 正常路径由 shutdown() 负责；若没走 shutdown 就析构，这里不重复释放，
    // 以免和库内部的 free 冲突造成 double free。
}

void BtmgBackend::setEvents(const Events &events)
{
    m_events = events;
}

bool BtmgBackend::initialize(int profileMask)
{
    // 日志默认走 stdout，会被 S70launcher 的重定向吃掉。demo 里是 bt_test
    // 自己调 btmg_set_log_file_path（bt_test.c:599），库没有默认值，
    // 产品里必须自己设，否则出问题没日志可查。
    static char logPath[] = "/tmp/btmg.log";
    btmg_set_log_file_path(logPath);
    bt_manager_set_loglevel(BTMG_LOG_LEVEL_WARNG);

    // preinit 只 malloc + act_init 起 action 线程，不碰硬件
    if (bt_manager_preinit(&m_callbacks) != 0) {
        m_status = QStringLiteral("bt_manager_preinit 失败");
        return false;
    }

    // ── profile 覆盖顺序（不能反）────────────────────────────────
    // set_default_profile(true) 读 /etc/bluetooth/bluetooth.json，对 10 个字段
    // 做整体赋值（bt_manager.c:214-223 全是 `=`）；
    // enable_profile(mask) 是逐位置 true（:165-200），没有把某项置 false 的接口。
    // 所以只能「先读 json 打底，再按需追加」。反过来会被整体赋值冲掉。
    // 运行期无法关闭 json 里已开的 profile，想改集合只能改文件或重启进程。
    bt_manager_set_default_profile(true);
    if (profileMask != 0)
        bt_manager_enable_profile(profileMask);
    m_profileMask = profileMask;

    m_callbacks->btmg_adapter_cb.adapter_state_cb = onAdapterStateCb;
    m_callbacks->btmg_gap_cb.gap_scan_status_cb   = onScanStatusCb;
    m_callbacks->btmg_gap_cb.gap_device_add_cb    = onDeviceAddCb;
    m_callbacks->btmg_gap_cb.gap_device_remove_cb = onDeviceRemoveCb;
    m_callbacks->btmg_gap_cb.gap_update_rssi_cb   = onUpdateRssiCb;
    m_callbacks->btmg_gap_cb.gap_bond_state_cb    = onBondStateCb;

    // 配对确认：不注册也能跑（bt_agent.c 每个 handler 都有 else 分支
    // 自动回 DefaultPincode），但那样用户看不到确认框、也不受控。
    m_callbacks->btmg_agent_cb.agent_request_confirm_passkey = onConfirmPasskeyCb;
    m_callbacks->btmg_agent_cb.agent_request_authorize       = onRequestAuthorizeCb;

    // init 只 register_callback + 建链表，不碰硬件。
    // 真正拉起 hci0 在 setEnabled(true) → bt_manager_enable(true)。
    if (bt_manager_init(m_callbacks) != 0) {
        m_status = QStringLiteral("bt_manager_init 失败");
        return false;
    }

    // 官方《Tina Linux 蓝牙开发指南》4.12 节的调用流程第 5 步：
    // 「调用 btmg_dev_list_new，创建已扫描设备列表」。
    //
    // 这一步只有 demo 需要——bt_test 自己维护一份发现列表供 scan_list 命令用。
    // 本模块不缓存设备（界面用 BtScanModel），所以不调它，理由记在这里，
    // 免得后来的人以为是漏了。设备列表的权威来源是 BlueZ，不是这个链表。

    m_initialized = true;
    m_status = QStringLiteral("已初始化");
    return true;
}

// 蓝牙名默认带本机地址后三位，避免多台设备同名时手机端分不清。
// 例：HMI-T113S4-2D29（地址 22:22:37:61:2D:29 的后两段）。
// bt_test 用的是 aw-bt-test-2D-29（bt_test.c:64-70），这里换成产品名。
void BtmgBackend::applyAdapterName()
{
    // 地址必须在**这个回调里**现取，不能用 setEnabled() 缓存的 m_localAddress。
    //
    // 踩过的坑：第一版在这里读 m_localAddress，结果手机上只显示
    // "HMI-T113S4"、没有地址后缀。原因是时序——
    // adapter_state_cb 的 On 比 bt_manager_enable() 返回**早 18ms**
    // （实测日志 33.373 vs 33.391），而 m_localAddress 是在 enable 返回之后
    // 才赋值的，回调运行时它还是空的。
    //
    // 在回调里直接调 bt_manager_get_adapter_address 是安全的：
    // api_action.c:82 判断「调用线程 == action 线程」时直接同步执行，
    // 不投队列、不会自锁。bt_test.c:62-64 就是这么写的。
    char buf[18] = {0};
    QString name = QStringLiteral("HMI-T113S4");
    if (bt_manager_get_adapter_address(buf) == 0) {
        const QString address = QString::fromLatin1(buf);
        if (address.size() == 17) {
            // 取最后两段去掉冒号：22:22:37:61:2D:29 → 2D29
            const QString tail = address.mid(12).remove(QLatin1Char(':'));
            name += QLatin1Char('-') + tail;
            m_localAddress = address;   // 顺手更新缓存，供 localAddress() 用
        }
    }

    // 官方文档 4.3.23：名称长度上限 248，超了会被库截断。
    // 这里最多 20 字符左右，不需要额外处理。
    const QByteArray utf8 = name.toUtf8();
    bt_manager_set_adapter_name(utf8.constData());
}

bool BtmgBackend::setEnabled(bool enabled)
{
    if (!m_initialized) {
        m_status = QStringLiteral("后端未初始化");
        return false;
    }

    // 实测耗时（板端 2026-11-18 日志）：enable(true) 到 BT is ON 共 3.67 秒。
    //   0.00s  bt_manager_enable 进入
    //   3.27s  init hci device successful（rfkill 复位 + hciattach 等待）
    //   3.66s  BT is ON
    //   3.67s  agent 注册完成
    // 之前的源码推算上界是十几秒，实测远好于上界。
    //
    // 失败时 btmanager 内部会调 bt_init.sh stop 拆掉整条栈
    // （bt_action.c:217,225 → platform.c:54），所以调用方不要「失败立即重试」。
    const int rc = bt_manager_enable(enabled);
    if (rc < 0) {
        m_status = QStringLiteral("上电失败：%1")
                       .arg(QString::fromUtf8(bt_manager_get_error_info(rc)));
        m_enabled = false;
        return false;
    }

    m_enabled = enabled;
    m_status = enabled ? QStringLiteral("控制器就绪") : QStringLiteral("已关闭");

    if (!enabled) {
        m_localAddress.clear();
    }
    // 上电时刻的地址缓存不在这里取：此时 On 回调已经先跑过了，
    // 由 applyAdapterName() 负责填 m_localAddress（见那里的说明）。
    return true;
}

bool BtmgBackend::isEnabled() const
{
    return m_enabled;
}

bool BtmgBackend::startScan(int scanType)
{
    if (!m_enabled) {
        // 库自己的前置检查是坏的（见头文件），必须在外面挡
        m_status = QStringLiteral("未上电，无法扫描");
        return false;
    }

    btmg_scan_filter_t filter;
    filter.uuid_list = nullptr;
    filter.uuid_num = 0;
    filter.rssi = -90;      // 与 demo 一致（bt_classic_demo.c:56）
    filter.type = (scanType == BtScanLe) ? BTMG_SCAN_LE : BTMG_SCAN_BR_EDR;
    bt_manager_scan_filter(&filter);

    // 注意：bt_adapter_start_scan 首句就是 bt_remove_unpaired_devices()
    // （bt_adapter.c:273），已发现未配对的会被从 BlueZ 缓存删掉，
    // 所以上层每次开始扫描都必须清空本地列表重来，不能指望累加。
    const int rc = bt_manager_start_scan();
    if (rc < 0) {
        m_status = QStringLiteral("扫描启动失败：%1")
                       .arg(QString::fromUtf8(bt_manager_get_error_info(rc)));
        return false;
    }
    m_scanning = true;
    m_status = QStringLiteral("扫描中");
    return true;
}

bool BtmgBackend::stopScan()
{
    if (!m_scanning)
        return true;
    bt_manager_stop_scan();
    m_scanning = false;
    m_status = QStringLiteral("扫描已停止");
    return true;
}

bool BtmgBackend::isScanning() const
{
    return m_scanning;
}

bool BtmgBackend::pairDevice(const QString &address)
{
    if (!m_enabled) {
        m_status = QStringLiteral("未上电，无法配对");
        return false;
    }

    QByteArray addr = address.toLatin1();
    // 同步阻塞点：bt_device_pair 内部先 _get_object_path → GetManagedObjects
    // 同步往返（common.c:134），设备多时会慢，但远不及 connect 的 15s。
    // 注意配对成功**不会**发 BONDED 回调（见 handleBondState 的说明）。
    const int rc = bt_manager_pair(addr.data());
    if (rc < 0) {
        m_status = QStringLiteral("配对失败：%1")
                       .arg(QString::fromUtf8(bt_manager_get_error_info(rc)));
        return false;
    }
    m_status = QStringLiteral("配对请求已发出");
    return true;
}

bool BtmgBackend::unpairDevice(const QString &address)
{
    QByteArray addr = address.toLatin1();
    const int rc = bt_manager_unpair(addr.data());
    if (rc < 0) {
        m_status = QStringLiteral("取消配对失败：%1")
                       .arg(QString::fromUtf8(bt_manager_get_error_info(rc)));
        return false;
    }
    m_status = QStringLiteral("已取消配对");
    return true;
}

bool BtmgBackend::connectDevice(const QString &address)
{
    if (!m_enabled) {
        m_status = QStringLiteral("未上电，无法连接");
        return false;
    }
    // 这条最慢：D-Bus Connect 超时 15000ms（bt_device.c:123），
    // 期间同一条 action 队列上的其它调用都会排队。
    QByteArray addr = address.toLatin1();
    const int rc = bt_manager_connect(addr.constData());
    if (rc < 0) {
        m_status = QStringLiteral("连接失败：%1")
                       .arg(QString::fromUtf8(bt_manager_get_error_info(rc)));
        return false;
    }
    m_status = QStringLiteral("已连接");
    return true;
}

bool BtmgBackend::disconnectDevice(const QString &address)
{
    QByteArray addr = address.toLatin1();
    const int rc = bt_manager_disconnect(addr.constData());
    if (rc < 0) {
        m_status = QStringLiteral("断开失败：%1")
                       .arg(QString::fromUtf8(bt_manager_get_error_info(rc)));
        return false;
    }
    m_status = QStringLiteral("已断开");
    return true;
}

QList<BtDeviceRecord> BtmgBackend::pairedDevices()
{
    QList<BtDeviceRecord> out;
    if (!m_initialized)
        return out;

    btmg_bt_device_t *devList = nullptr;
    int count = 0;
    if (bt_manager_get_paired_devices(&devList, &count) < 0 || devList == nullptr)
        return out;

    for (int i = 0; i < count; ++i) {
        const BtDeviceRecord record = toRecord(&devList[i]);
        if (record.isValid())
            out.append(record);
    }

    // 库内部 malloc 分配，必须用配套释放函数，否则这块内存在 action 线程堆上泄漏
    bt_manager_free_paired_devices(devList, count);
    return out;
}

void BtmgBackend::replyPasskey(quintptr handle, bool accept)
{
    if (handle == 0)
        return;

    void *invocation = reinterpret_cast<void *>(handle);
    if (!accept) {
        bt_manager_agent_send_pair_error(invocation, BT_PAIR_REQUEST_REJECTED,
                                         "rejected on HMI");
        m_status = QStringLiteral("用户拒绝配对");
        return;
    }

    // 确认类请求回空响应即可（bt_agent.c:122-130 的 confirm 分支就是这么回的）
    bt_manager_agent_pair_send_empty_response(invocation);
    m_status = QStringLiteral("已确认配对");
}

QString BtmgBackend::localAddress()
{
    // 返回 setEnabled 时缓存的地址。
    // 不在每次调用时问库：那是一次 act_transfer 同步往返，
    // 会被同队列上的 connect（最长 15s）拖住。
    return m_localAddress;
}

void BtmgBackend::shutdown()
{
    if (!m_initialized)
        return;
    // deinit 必须和 enable 在同一个工作线程上调用（同为 act_transfer 同步投递）
    bt_manager_deinit(m_callbacks);
    m_callbacks = nullptr;
    m_initialized = false;
    m_enabled = false;
    m_scanning = false;
}

QString BtmgBackend::backendName() const
{
    return QStringLiteral("btmg");
}

QString BtmgBackend::statusText() const
{
    return m_status;
}

// ── 事件处理（在 btmanager 线程上执行，只转发不阻塞）─────────────

void BtmgBackend::handleAdapterState(int state)
{
    m_enabled = (state == BtAdapterOn);

    // agent 注册要放在这里，不能放在 setEnabled() 返回之后。
    //
    // 依据板端实测日志（2026-11-18）：
    //   00:01:33.373  BT is ON            ← 回调里已经可以注册
    //   00:01:33.391  注册完成
    // 而 bt_manager_enable() 要到 33.391 之后才返回。也就是说在
    // setEnabled() 里调，是在 action 线程已经走完一遍之后再重入它一次；
    // 放在 On 回调里则与 bt_test 的做法一致（bt_test.c:62-74 就是在
    // adapter_status_cb 的 BTMG_ADAPTER_ON 分支里调 set_io_capability）。
    //
    // 顺带这一步内部会调 RequestDefaultAgent，所以不能和别的 agent
    // （比如 bluetoothctl）同时抢 default。
    if (state == BtAdapterOn) {
        // ── 官方《Tina Linux 蓝牙开发指南》4.12 节 9 步流程的收尾 ──
        // 第 7 步 set_adapter_name、第 8 步 set_io_capability、
        // 第 9 步 set_scan_mode，全部在 On 之后做（bt_test.c:57-75 同此顺序）。
        // 这些调用都要求适配器已上电，放在 enable 返回之后会撞上
        // 「库的前置检查失效」那个坑，所以必须在这里。

        // 第 7 步：设置蓝牙名，手机端看到的就是它
        applyAdapterName();

        // 第 8 步：io 能力。有触摸屏用 KEYBOARDDISPLAY，让用户核对 6 位码。
        // 内部会调 RequestDefaultAgent，不能和 bluetoothctl 抢。
        const int rc = bt_manager_agent_set_io_capability(BTMG_IO_CAP_KEYBOARDDISPLAY);

        // 第 9 步：发现模式。默认 CONNECTABLE_DISCOVERABLE，
        // 否则手机搜不到本机、也连不上来。
        // 官方 4.2.3 的三种模式：
        //   NONE                    不可被发现、不可被连接
        //   CONNECTABLE             可被连接、不可被发现
        //   CONNECTABLE_DISCOVERABLE 可被发现、可被连接
        bt_manager_set_scan_mode(BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE);

        m_status = (rc == 0) ? QStringLiteral("控制器就绪")
                             : QStringLiteral("控制器就绪（agent 注册失败）");
    }

    if (m_events.adapterState)
        m_events.adapterState(state);
}

void BtmgBackend::handleScanState(int state)
{
    m_scanning = (state == BtScanStarted);
    if (m_events.scanState)
        m_events.scanState(state);
}

void BtmgBackend::handleDeviceAdded(const btmg_bt_device_t *device)
{
    if (!m_events.deviceFound)
        return;
    m_events.deviceFound(toRecord(device));
}

void BtmgBackend::handleDeviceRemoved(const btmg_bt_device_t *device)
{
    if (!m_events.deviceRemoved)
        return;
    const BtDeviceRecord record = toRecord(device);
    if (record.isValid())
        m_events.deviceRemoved(record.address);
}

void BtmgBackend::handleRssi(const char *address, int rssi)
{
    if (!m_events.rssiUpdated || !address)
        return;
    m_events.rssiUpdated(QString::fromLatin1(address), rssi);
}

void BtmgBackend::handleBondState(int state, const char *address)
{
    if (!m_events.bondState || !address)
        return;

    // 已知缺陷：配对成功的 BONDED 回调经常永远不来。
    // bluez_app_callback_handle 第一句是 `if (device->r_class == 0) return;`
    // （bt_bluez_signals.c:133），而 BONDED 的唯一来源就是这里；
    // bt_device_pair_callback 成功路径一个回调都不发（bt_device.c:47-66）。
    // r_class 是 BR/EDR Class，BLE 设备和不少手机都是 0。
    //
    // 所以上层绝不能把「收到 BONDED」当作配对成功的唯一判据，
    // 必须另起定时器轮询 pairedDevices() 兜底（BtService 里已实现）。
    m_events.bondState(state, QString::fromLatin1(address));
}

void BtmgBackend::handleConfirmPasskey(void *handle, char *device,
                                       unsigned int passkey, bool numericOnly)
{
    if (!m_events.passkeyRequested) {
        // 没有界面接手时立刻回默认响应，不能让 BlueZ 干等
        bt_manager_agent_pair_send_empty_response(handle);
        return;
    }
    // handle 原样带走，由界面处理完后经 replyPasskey 交回。
    // 绝不能在这里等用户 —— 会把整条 bluez mainloop 停住，
    // 对端约 25s 超时失败。
    m_events.passkeyRequested(reinterpret_cast<quintptr>(handle),
                              QString::fromLatin1(device ? device : ""),
                              passkey, numericOnly);
}
