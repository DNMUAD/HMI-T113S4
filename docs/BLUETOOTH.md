# 蓝牙模块（btmanager 4.0.5）

目标板：全志 T113-S4 + AIC8800D80，BlueZ 5.15 工具链 + 全志 `libbtmg.so`。

本文记录**为什么这么写**，以及从 SDK 源码核对出来的库行为。所有结论都标了出处（文件:行号），
源码位置是 `platform/allwinner/wireless/btmanager/src/` 与 `platform/allwinner/wireless/common/src/api_action/`。

---

## 一、分层

```
pages/btpage.*          界面：按钮、列表、配对确认面板
        ↓ 只调方法、只连信号
services/btservice.*    界面侧唯一入口；三个状态位在这里维护
        ↓ 信号槽（Qt::QueuedConnection）
services/btworker.*     工作线程宿主；所有阻塞调用发生在这条线程
        ↓ 虚接口
services/ibtbackend.h   IBtBackend（策略 + 空对象）
        ├─ btmgbackend.*    板端真实现，直调 libbtmg（仅 HMI_HAVE_BT 编译）
        └─ fakebtbackend.*  桌面/--simulate 的预置数据
        ↓
models/btscanmodel.*    QAbstractListModel：设备记录 → 列表行
core/btdevice.h         BtDeviceRecord 值对象 + 事件枚举
```

依赖方向仍然向下。`pages/` 不包含任何 `bt_manager.h`，`models/` 也不依赖 `services/`
（值对象和枚举放在 `core/`）。

---

## 二、为什么必须有工作线程

`bt_manager.c:57`：

```c
act_init(&bt_act_handle, "bt_action", true);   // 第三个参数 is_sync = true
```

同步语义在 `common/src/api_action/api_action.c`：

- `:108-113` 非 action 线程调用会投队列，然后 `os_net_sem_wait(&handle->sem, OS_NET_WAIT_FOREVER)`；
- `:64` 前面还套了一层递归互斥锁。

所以**每个 `bt_manager_*` 都是同步阻塞的**，而且全部挤在**同一条** action 队列上（长度 24，`api_action.h:44`）。

`bt_manager_enable(true)` 这条链路里包含：

| 环节 | 出处 | 耗时特征 |
|---|---|---|
| `system("/etc/bluetooth/bt_init.sh start")` | `platform.c:33` | rfkill 复位 2s + hciattach 后轮询 hci0 最多 70×100ms |
| `bt_bluez_init()` | `bt_bluez.c:143-151` | 每 200ms 轮询等 bluez mainloop 起来 |
| `bt_init_profile()` | `bt_action.c:89-113` | killall bluealsa 再 50ms 轮询等进程 |

**板端实测（2026-11-18，`bt_test -i -p a2dp-sink -d5` 日志）**：

| 时刻 | 事件 | 累计 |
|---|---|---|
| 00:01:29.718 | `_bt_manager_enable` 进入 | 0 |
| 00:01:32.990 | `bt_platform_init` 成功，hci 就绪 | **3.27s** |
| 00:01:33.373 | `BT is ON`（`bt_adapter_get_power_state`） | **3.66s** |
| 00:01:33.391 | agent 注册完成 | **3.67s** |

按源码推算的上界是十几秒，实测 3.67 秒。其中 3.27 秒花在
`bt_init.sh start`（rfkill 复位 2s + hciattach 等 hci0），后面 0.4 秒是
bluez mainloop + bluealsa 起来。

3.7 秒放在 GUI 线程仍会造成明显卡顿，所以工作线程照旧。

`connect` 才是真正的大头：`bt_device.c:123` 的 D-Bus 超时是 **15000ms**。
那 15 秒里同一条 action 队列上的任何其它调用都会排队，所以 `BtPage`
在连接期间置 busy 挡连点。

**验证**：`/tmp/blocking.cpp` 用 16ms 心跳定时器跑完整流程（上电→扫描→配对→确认），
实测 `maxGap=17ms`，GUI 线程全程未出现卡顿。

### 官方 9 步流程对照

《Tina Linux 蓝牙开发指南》v4.0 第 4.12 节给出的顺序：

| # | 官方步骤 | 本实现 |
|---|---|---|
| 1 | `bt_manager_preinit` | `initialize()` |
| 2 | `bt_manager_enable_profile` | `initialize()`，先 `set_default_profile` 再追加 |
| 3 | 填充 `btmg_callback_t` | `initialize()` |
| 4 | `bt_manager_init` | `initialize()` |
| 5 | `btmg_dev_list_new` | **不调**，见下 |
| 6 | `bt_manager_enable` | `setEnabled(true)` |
| 7 | `bt_manager_set_adapter_name` | `handleAdapterState` 的 On 分支 |
| 8 | `bt_manager_agent_set_io_capability` | `handleAdapterState` 的 On 分支 |
| 9 | `bt_manager_set_scan_mode` | `handleAdapterState` 的 On 分支 |

第 5 步不调的理由：`btmg_dev_list_new` 建的链表是 demo 自用的扫描缓存
（`bt_test` 的 `scan_list` 命令读它）。本模块的设备数据源是 `BtScanModel`，
权威来源是 BlueZ，多维护一份链表只会带来说不一致的风险。

第 7–9 步必须在 `adapter_state_cb` 的 `BTMG_ADAPTER_ON` 分支里做，
不能等 `bt_manager_enable()` 返回——实测 On 回调比它返回早 18ms，
而且这三个 API 都要求适配器已上电，否则会撞上「前置检查失效」那个坑。

### 上板验证结果（2026-11-18）

| 项 | 结果 |
|---|---|
| 上电、状态灯变绿 | 通过 |
| 手机能发现并配对 | 通过 |
| 扫描列出周围设备（电脑） | 通过 |
| 配对 6 位码面板弹出 | 通过 |
| 蓝牙名带地址后缀 | **未通过**，详见下 |

**蓝牙名只有 `HMI-T113S4`、没有 `-2D29` 后缀**，原因是一个时序 bug：

`applyAdapterName()` 最初读的是 `setEnabled()` 里缓存的 `m_localAddress`，
但 On 回调比 `bt_manager_enable()` 返回早 18ms，回调执行时那个缓存还是空的，
于是走了 `addr.size() != 17` 的兜底分支，只设了裸名字。

修法：在回调里直接调 `bt_manager_get_adapter_address()`。这样做是安全的——
`api_action.c:82` 判断「调用线程 == action 线程」时直接同步执行，
不投队列、不会自锁；`bt_test.c:62-64` 就是这么写的。

教训：**凡是 `adapter_state_cb` 里要用到的数据，都必须在该回调内现取，
不能依赖 `bt_manager_enable()` 返回后才准备好的任何缓存。**

---

## 三、三个自己维护的状态位

### adapterOn

`bt_manager.c` 里 **55 处**这样的前置检查：

```c
if (check_bt_is_on == false)
    return BT_ERROR_ADATER_NOT_ON;
```

`check_bt_is_on` 是**函数**（`bt_manager.c:239`），不是宏。函数指针和 `false` 比较恒为假，
所以这 55 处检查全部失效。

后果：没上电就调 `start_scan`，会真的执行到 `bt_adapter_start_scan`（`bt_adapter.c:265`），
此时 `bluez_mg.dbus` 是 NULL，`g_dbus_connection_call_sync(NULL, ...)` 返回 NULL，
而 `g_dbus_call_sync_check_error(NULL)` 直接 `return BT_OK`（`common.c:110`）。

**拿到「成功」，但什么都没发生。** 必须自己维护状态位，不能依赖库的前置检查。

另：`bt_manager_get_adapter_state()` 未上电时把错误码 `-2` 强转成 `btmg_adapter_state_t`
返回（`bt_manager.c:392-402`，枚举里没有 -2），语义不可靠。状态改由
`adapter_state_cb` 维护，查询走缓存。

### scanning

由 `gap_scan_status_cb` 维护。原因同上：库的 `is_scanning` 查询也会走同步队列。

### bonding

**配对成功的 BONDED 回调可能永远不来。** 两个原因叠加：

1. `bluez_app_callback_handle` 第一句是 `if (device->r_class == 0) return;`
   （`bt_bluez_signals.c:133`），而 BONDED 的**唯一**来源就是这里。
   `r_class` 是 BR/EDR Class，BLE 设备和不少手机都是 0。
2. `bt_device_pair_callback`（`bt_device.c:47-66`）只在 `error != NULL` 时发 BOND_FAILED，
   **成功路径一个回调都不发**。

所以 `BtService` 在 `pair()` 之后起 15s 兜底定时器，超时后主动 `reloadPaired()`
读一次已配对列表，而不是把「等到 BONDED」当判据。

---

## 四、回调线程与转 GUI

回调不在同一线程，三处来源：

| 回调 | 线程 | 出处 |
|---|---|---|
| `adapter_state_cb` | action 线程 | `bt_action.c:206/237/272` |
| `gap_*` / a2dp / hfp / spp | bluez GMainLoop 线程 | `bt_bluez.c:44 bt_routine` |
| GATT | gatt mainloop 线程 | `bt_mainloop.c` |

`BtWorker` 在回调里只做两件事：深拷贝、emit 信号。Qt 的 `AutoConnection` 会按
「发送线程 ≠ 接收对象线程」判成 `QueuedConnection`，`postEvent` 本身线程安全。

**注册元类型是必须的**：`quintptr` 不注册的话 Qt 会打印
`Cannot queue arguments of type 'quintptr'` 并**丢弃**整个 queued 调用。
实测表现是配对确认面板永远不弹。`BtService` 构造里已注册
`BtDeviceRecord`、`QList<BtDeviceRecord>`、`quintptr` 三个。

---

## 五、内存与字段可信度

`btmg_bt_device_t` 来自 `common.c:231 _get_device_properties`，只取 9 个 BlueZ 属性。

**必须当场深拷贝**：`bluez_signals.c:89-92` 的流程是

```c
btmg_bt_device_t device;                       // 栈上
_get_device_properties(properties, &device);
bluez_device_changed_callback(&device, true);
bt_free_device(&device);                       // 回调一返回就释放
```

`remote_address` / `remote_name` / `icon` 三个指针在回调返回后立刻失效。

字段可信度：

| 字段 | 可信 | 说明 |
|---|---|---|
| `address` | 是 | 唯一键，大小写不敏感 |
| `paired` / `connected` / `trusted` / `blocked` | 是 | |
| `name` | 部分 | **没名字的设备根本不进发现回调**（`bt_bluez_signals.c:63` 的 `if (device->remote_name == NULL) return;`） |
| `rssi` | 部分 | **实测发现回调里就有值**（-52 / -89），不是 0；但 BlueZ 只对已缓存设备持续上报 `PropertiesChanged`，不能保证每台都有。实时值走 `gap_update_rssi_cb` |
| `icon` | 是 | BlueZ 分类图标名（`audio-card`/`phone`/`computer`），不是图片路径 |

界面处理：`rssi == 0` 一律显示 `--`，不当 0 dBm；名字为空回退显示地址。

### 已知缺陷：名字后到的设备永久丢失

板端日志（2026-11-18）里有一台 OPPO A93s 5G：

```
00:02:32.655  address:14:9B:F3:B8:27:72, name:(null), rssi:-89   ← 首次发现，无名字
00:02:48.795  {'RSSI': -89, 'Name': 'OPPO A93s 5G', 'Alias': ...}  ← 16 秒后名字到了
```

但整个日志里**再没有这台设备的 `bt_test_dev_add_cb`**，`scan_list` 最终只列出 iQOO 一台。

原因：`bt_bluez_signals.c:63` 的 `if (device->remote_name == NULL) return;`
让首次发现被丢弃；而名字后到时走的是 `_device_properties_changed`
（`bt_bluez_signals.c:293-313`），它只处理 `Connected`、`Paired`、`RSSI`
三个属性，**不处理 Name**，也不会补发发现回调。

**后果**：广播里不带名字的设备，本轮扫描永远进不了列表，`BtScanModel` 无从得知。

**这是库的行为，界面层无法补救。** 唯一绕开的办法是自己连 D-Bus 监听
`InterfacesAdded` / `PropertiesChanged`，那等于放弃 libbtmg 改用 GDBus 重写，
和现有架构冲突，暂不采用。

如果需要确认某台设备确实存在，只能靠 `bluetoothctl devices`
（BlueZ 自己记得，只是 btmanager 没往外抛）。

---

## 六、配对确认（agent）

`handle` 就是 BlueZ 的 `GDBusMethodInvocation*`（`bt_agent.c:313-318` 强转）。
BlueZ 在等这个 D-Bus 方法返回，**不回复整条 bluez mainloop 就停住**，对端约 25s 超时。

所以：

- 回调里把 `handle` 原样带走，立刻返回，**绝不等用户**；
- 界面用 `QStackedWidget` 切到确认面板（板端 linuxfb 单窗口，不能弹 QDialog）；
- 20s 兜底定时器，超时按「拒绝」处理；
- `handle` 只能用一次，回复后置 0。

回复方式：
- 确认 → `bt_manager_agent_pair_send_empty_response(handle)`
- 拒绝 → `bt_manager_agent_send_pair_error(handle, BT_PAIR_REQUEST_REJECTED, ...)`

另注意：`bt_manager_agent_pair_send_empty_response` 内部第一句就是
`check_bt_is_on()`（`bt_manager.c:926`），而它本身又是同步投递——
回复时会去抢 action 线程的锁。窗口很窄但存在，所以回复动作放在工作线程上做。

IO 能力用 `BTMG_IO_CAP_KEYBOARDDISPLAY`：有触摸屏，让用户核对 6 位码。

### agent 必须在 `adapter_state_cb` 的 On 分支里注册

不能放在 `bt_manager_enable()` 返回之后。日志时序：

```
00:01:33.373  BT is ON        ← 回调已经可以注册
00:01:33.391  注册完成
```

`bt_manager_enable()` 要到 33.391 之后才返回。在 `setEnabled()` 里调，
等于等 action 线程走完一遍之后再重入它一次；而放在 On 回调里与
`bt_test` 的做法一致（`bt_test.c:62-74` 就是在 `adapter_status_cb` 的
`BTMG_ADAPTER_ON` 分支里调 `set_io_capability`，并按前台的
`KEYBOARDDISPLAY` / 后台的 `NOINPUTNOOUTPUT` 二选一）。

`BtmgBackend::handleAdapterState()` 已按这个顺序实现。

---

## 七、profile

`bt_manager_set_default_profile(true)` 读 `/etc/bluetooth/bluetooth.json`，
对 10 个字段做**整体赋值**（`bt_manager.c:214-223` 全是 `=`）。

`bt_manager_enable_profile(mask)` 是逐位**置 true**（`:165-200`），没有把某项置 false 的接口。

所以顺序只能：

```cpp
bt_manager_set_default_profile(true);   // 先按 json 打底
if (profileMask != 0)
    bt_manager_enable_profile(profileMask);  // 再追加
```

反过来前一步会把追加的全冲掉。运行期**无法关闭** json 里已开的 profile，
改集合只能改文件或重启进程。

板端当前 `/etc/bluetooth/bluetooth.json`：

```json
"profile": { "a2dp_sink":1, "avrcp":1, "a2dp_source":0, "hfp_hf":0,
             "hfp_ag":0, "spp":0, "gatt_client":0, "gatt_server":0, "smp":0 }
```

`BtService` 目前传 `profileMask = 0`，即完全跟随 json。

已知缺陷（`bt_config_json.c:37-38`）：`spp_client` 和 `spp_server` 都读同一个 `"spp"` 键。
设了这个键会同时打开两个 SPP profile。

---

## 八、扫描的副作用

`bt_adapter_start_scan` 第一句就是 `bt_remove_unpaired_devices()`（`bt_adapter.c:273`），
它遍历所有设备、每个调两次同步 D-Bus（`bt_device.c:380-401`）。

后果：

1. 已发现但未配对的设备会被从 BlueZ 缓存删掉，**本地列表必须每次重来**，不能累加；
2. 设备多时扫描启动本身会变慢。

`BtPage` 在点「扫描」时先 `m_scanModel->clear()`，就是为这个。

---

## 九、构建

`.pro` 的 ARM 分支：

```qmake
contains(QT_ARCH, arm) {
    TARGET   = QtApplication
    DEFINES += HMI_DEVICE_TARGET
    DEFINES += HMI_HAVE_BT
    LIBS    += -lbluetooth -ljson-c -lbtmg -ldl -lm -lresolv -lwirelesscom
    LIBS    += -lglib-2.0 -lgio-2.0 -lgobject-2.0 -lasound -ldbus-1
    LIBS    += -lbluetooth-internal -lshared-mainloop
} else {
    TARGET   = HMI-T113S4
    DEFINES += HMI_DESKTOP
}
```

### 为什么不能只写 `-lbtmg`

`libbtmg.so` 是**不完整的**：它把 bluez 的部分内部实现和整条 GATT 协议栈
留给了外部解析。只写 `-lbtmg` 会在链接阶段炸出上百条 undefined reference：

```
libbtmg.so: undefined reference to `gatt_db_remove_service'
libbtmg.so: undefined reference to `bt_gatt_client_new'
libbtmg.so: undefined reference to `mainloop_quit'
libbtmg.so: undefined reference to `log_print'
libbtmg.so: undefined reference to `json_object_get_int'
...
```

完整列表与顺序取自 SDK 的 demo Makefile
（`platform/allwinner/wireless/btmanager/demo/Makefile:3-17`）。
顺序有讲究：`-lbtmg` 必须在依赖它的库之前，静态库
`-lbluetooth-internal` / `-lshared-mainloop` 必须放最后——
GNU ld 是单遍扫描，放在前面就不会回头再解析。

**已验证**：用交叉工具链完整编译并链接通过，产出
`ELF 32-bit LSB executable, ARM, EABI5`，`readelf -d` 里 20 个 NEEDED
全部解析成功，且这些库在板端 `target/usr/lib` 中全部存在。

`services/services.pri` 里用 `contains(DEFINES, HMI_HAVE_BT)` 决定是否编入
`btmgbackend.*`。注意 qmake 的 `DEFINES` 只是编译宏，**不会**自动生成同名 scope 变量，
所以不能写成 `HMI_HAVE_BT { ... }`。

路径核对（`qmake -query QT_SYSROOT` 返回的 sysroot）：

```
host/arm-buildroot-linux-gnueabi/sysroot/usr/include/bt_manager.h    42065 字节
host/arm-buildroot-linux-gnueabi/sysroot/usr/lib/libbtmg.so        1243016 字节
```

两者都在默认搜索路径下，不需要额外 `INCLUDEPATH`/`LIBPATH`。
`out/.../buildroot/staging` 是指向该 sysroot 的软链，两处指向同一批文件。

桌面 Kit 不链接 libbtmg，`btBackendCompiledIn()` 返回 false，
`createBtBackend()` 落到 `FakeBtBackend`。

---

## 十、桌面验证

```bash
# 离屏跑蓝牙页（page 序号 2）
QT_QPA_PLATFORM=offscreen ./HMI-T113S4 --windowed --size 800x480 --simulate \
    --page 2 --shot /tmp/bt.png
```

`FakeBtBackend` 刻意复刻了真实现的时序特征，用来在桌面验这些点：

- 上电要等（1.5s，真机十几秒），期间显示「处理中」；
- 扫描是陆续发现（每 400ms 一台），不是一次性给全表；
- 有一台带 passkey，走完整确认流程；
- 有一台 rssi 为 0，验证 `--` 显示；
- 有一台没名字，验证回退显示地址；
- 已配对列表里放了一台空名设备，验证兜底。

实测结果（本机 x86_64 + Qt 5.15.2）：

| 项 | 结果 |
|---|---|
| 桌面编译 | 通过，0 error 0 warning |
| **ARM 交叉编译 + 链接** | **通过**，产出 ARM 32 位 ELF，20 个 NEEDED 全解析 |
| 上电 → 扫描 → 配对确认 → 已配对 | 全部按预期 |
| 800x480 溢出（6 种状态各测一次） | 全部 `overflow=0` |
| GUI 线程阻塞 | `maxGap=17ms`，未卡顿 |

ARM 侧的验证方法（桌面 Kit 覆盖不到，必须单独跑）：

```bash
S=out/t113_s4/100ask/buildroot/buildroot
$S/host/bin/qmake HMI-T113S4.pro -spec devices/linux-buildroot-g++
make -j8
readelf -d QtApplication | grep NEEDED
```

这轮交叉编译抓出了两个桌面编译器发现不了的错误：

1. `btmg_bt_device_t` 是**匿名 struct 的 typedef**，前置声明会造出同名无关类型，
   让 `bt_manager.h` 报 `conflicting declaration`。改成直接 include 厂商头。
2. `btmg_set_log_file_path` 声明在 `bt_log.h`，不在 `bt_manager.h`。

**建议每次改蓝牙相关代码都跑一次 ARM 编译**，桌面通过不代表板端能编。

---

## 十一、尚未确认（需要上板实测）

以下几条只能实测定论，方案里已按「保守假设」处理：

1. ~~`enable(true)` 真实耗时~~ —— **已实测 3.67 秒**（见第二节表格）。

2. **`r_class == 0` 覆盖多少设备（部分确认）。** 实测 iQOO Neo9S Pro+ 与 OPPO A93s 5G
   的 `class` 都是 `5898764` = `0x5A020C`，非 0，说明常见手机在 BR/EDR 发现阶段
   是带 Class 的，BONDED 回调有机会正常到达。但 BLE 设备（手环、耳机）通常没有
   r_class，那类设备的配对仍要靠 15s 轮询兜底，未实测。

3. **`aic8800_btlpm.ko` 装上了但 probe 没跑（未解决，不影响功能）。**

   已确认的部分：模块装载成功（`lsmod` 里 `aic8800_btlpm 20480 0`），
   `S41wifi` 的加载逻辑也对了；dts 节点存在且 `status = "okay"`
   （烧写的 `sunxi.fex` 里有 `btlpm@0` + `allwinner,sunxi-btlpm`），
   `&uart1` 也是 okay，pins 为 PG6-PG9、与 PG13/PG14 不冲突。

   但 `/proc/bluetooth` 目录始终不存在，`bt_init.sh:17` 与 `:96` 都报
   `No such file or directory`，且三份日志里**一条 `[BT_LPM]` 都没有**。
   `Used by 0` 说明模块没有代码持有，而 `proc_mkdir` 失败会打
   `Unable to create /proc/bluetooth directory`——这句也没出现，
   所以判断是 `bluesleep_probe` 根本没被调用。

   待查（上板跑）：
   ```bash
   ls /proc/device-tree/ | grep -i bt      # 节点在不在
   dmesg | grep -iE "btlpm|BT_LPM|bluesleep|sunxi-btlpm"
   ```

   **影响面**：btlpm 只管低功耗休眠唤醒。日志证明即使它没起来，
   rfkill 复位、bluetoothd 启动、hci0 就绪、扫描、配对全部正常。

4. **`act_init` 的 `is_sync=true` 是否可以被绕过。** 如果改库或换成异步封装能拿到
   非阻塞语义，工作线程的必要性会下降。但那是改厂商库，不在本次范围。

5. **配对成功后的连接行为。** `bt_manager_connect` 走 `bt_device_connect`，
   内部会先停扫描再 D-Bus Connect。扫描与连接交错时的真实时序没实测。

---

## 十二、改动清单

新增：

- `core/btdevice.h` —— 值对象与事件枚举
- `services/ibtbackend.h` —— 后端接口
- `services/btbackendfactory.cpp` —— 工厂
- `services/btworker.h/.cpp` —— 工作线程宿主（Q_OBJECT）
- `services/btmgbackend.h/.cpp` —— 板端真实现（仅 ARM 编译）
- `services/fakebtbackend.h/.cpp` —— 桌面假实现（Q_OBJECT）
- `models/btscanmodel.h/.cpp` —— 列表模型
- `docs/BLUETOOTH.md` —— 本文

修改：

- `services/btservice.h/.cpp` —— 重写为「不阻塞的门面 + 工作线程编排」
- `pages/btpage.h/.cpp` —— 重做界面（列表/配对确认互斥、不弹窗）
- `services/services.pri`、`models/models.pri`、`core/core.pri` —— 登记新文件
- `HMI-T113S4.pro` —— ARM 分支加 `HMI_HAVE_BT` 与 `-lbtmg`
- `mainwindow.cpp` —— 状态栏加一个 BT 状态灯

未改动：WiFi / ETH / ADB 的实现、`ProcessRunner`、`StatusBus`、页面注册机制、
`tools/verify.py`。

`BtService` 保留了 `refresh()` 的原有签名（`IDeviceService` 要求），
但语义变了：**不再现场查询**，只摊已知状态。这一点在它自己的注释里写清楚了。
