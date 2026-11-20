# GPADC 按键模块（K1 / K2 / K3）

目标板：全志 T113-S4（100ask T113s4-SdNand，Tina 5.x + Linux 5.4）。
本文记录**为什么这么写**，以及从 SDK 源码核对出来的行为。结论都标了出处（文件:行号），
SDK 路径相对 `~/t113-tin5-v1.2/`。

---

## 一、分层与职责

```
pages/gpadcpage.*        界面：设备回显、按下计数、每个键一个「动作」下拉框
        ↓ 只调方法、只连信号
services/gpadcservice.*  设备发现 + 事件读取 + 键码翻译 + 按绑定派发动作
        ↓ 内核接口（无第三方库）
/proc/bus/input/devices 与 /dev/input/eventN
```

页面不认识任何业务动作，服务也不认识。两者之间只有三样东西：
`keys()`（有哪几个键）、`actions()`（可选动作表）、`binding(code)`（当前绑定）。

---

## 二、内核侧：键是怎么变成 event 的

整条链路已经就绪，**不需要改内核**：

| 环节 | 位置 | 说明 |
|---|---|---|
| 配置开关 | `config-5.4:1652` | `CONFIG_SUNXI_GPADC=y`（已开，实测编译进内核） |
| 驱动源码 | `drivers/input/sensor/sunxi_gpadc.c` | `Makefile:11` 的 `obj-$(CONFIG_SUNXI_GPADC)` |
| 设备树 | `device/config/chips/t113_s4/configs/100ask/linux-5.4/board.dts` 的 `&gpadc` | `status = "okay"` |
| 键值定义 | 同上，`key_cnt` / `keyN_vol` / `keyN_val` | 电压区间 → 键码 |
| 输入设备名 | `sunxi_gpadc.c:955` | `input_dev->name = "sunxi-gpadc0"` |
| 上报方式 | `sunxi_gpadc.c:1336-1338` | 低阈中断里 `input_report_key(..., 1)` + `input_sync()`，即按下 |
| 松开方式 | `sunxi_gpadc.c:1378-1384` | 高阈中断里 `input_report_key(..., 0)`：按下后驱动才 `sunxi_enable_higirq_ch_select`（`:1374`），电压回升过阈才报松开 |

按下与松开**都有**上报，`value` 分别是 1 和 0。中间还有一层 `filter_cnt`
抖动过滤（`:1301-1341`），连续 `filter_cnt` 次采样落在同一区间才算一次有效按键。

### 三个键的实测键码

板端 `hexdump /dev/input/eventN` 看到的是 4 位十六进制，**不要**直接当十进制：

| 按键 | hexdump | 十六进制 | 十进制（`input_event.code`） | 内核 `input-event-codes.h` 里的名字 |
|---|---|---|---|---|
| K1 | `0073` | 0x73 | **115** | `KEY_VOLUMEUP` |
| K2 | `0077` | 0x77 | **119** | `KEY_PAUSE` |
| K3 | `0175` | 0x175 | **373** | `KEY_MODE` |

与 `board.dts` 的对应（`&gpadc` 段）：

```
key0_vol = <210>;  key0_val = <115>;      → K1
key1_vol = <410>;  key1_val = <114>;      → 未在本板使用
key2_vol = <590>;  key2_val = <119>;      → K2
key3_vol = <750>;  key3_val = <373>;      → K3
key4_vol = <880>;  key4_val = <28>;       → 未在本板使用
```

驱动按 `keyN_vol` 算分压中点（`sunxi_gpadc.c:1053` 的 `set_vol[i]`），
按下时落在哪个区间就上报对应的 `keyN_val`。所以改键码要连同 `keyN_vol` 一起理解，
只改 `keyN_val` 是安全的（区间不变，只换上报值）。

### 键码**没有**业务含义

`KEY_VOLUMEUP` / `KEY_PAUSE` / `KEY_MODE` 只是驱动作者当年挑的名字。
本项目**不按 `KEY_*` 语义派发**，一律走「绑定表 → 动作」，
所以这三个键可以被绑到任意动作，不需要改 dts、也不需要重编内核。

---

## 三、应用侧的两个坑

### 坑 1：input 设备名是 `sunxi-gpadc0`，不是 `gpadc`

官方文档（dshanpi T113i-Industrial 的 GPADC 章节）给的匹配方式是
`Name="gpadc"`。实际 `/proc/bus/input/devices` 里是：

```
I: Bus=0019 Vendor=0001 Product=0001 Version=0100
N: Name="sunxi-gpadc0"
P: Phys=sunxigpadc0/input0
S: Sysfs=/devices/virtual/input/input2
H: Handlers=kbd event2
B: PROP=0
B: EV=100003
B: KEY=200000 0 0 0 0 0 0 0 8c0000 0 0 10000000
```

`gpadc` 是驱动的**平台设备名**（`sunxi_gpadc.c:1873` 的 `.name = "gpadc"` 与
`:2210` 的 `platform_driver .name = "sunxi-gpadc"`），不是 input 设备名。

第一版按 `Name="gpadc"` 精确匹配，结果永远返回空，
页面显示「未找到 gpadc 输入设备」。现在改成**名字含 `gpadc`、大小写不敏感**，
既能命中 `sunxi-gpadc0`，也兼容以后别的板子。

### 坑 2：`eventN` 必须从 `H: Handlers=` 行取

同一段里 `S: Sysfs=/devices/virtual/input/input2` 也含 `input` 字样，
整段正则容易抓成别的编号。`H: Handlers=kbd event2` 才是真节点，
而且有的设备有多个 handler（`kbd event0`），要取 `eventN` 那一个。

### 事件结构体大小

`sizeof(struct input_event)` 在 32 位 ARM 上是 **16 字节**，64 位桌面是 24 字节。
所以 `read()` 的长度用 `sizeof`，不要写死 16 —— 桌面 Kit 上写死会一直读到错的边界。

---

## 四、动作表与绑定（本模块的扩展点）

### 为什么不在代码里写死「K1 = 音量+」

按键到动作的关系是**产品配置**，不是硬件事实。写死的代价：
每次调整都要改代码、重编、重新部署整包；而且板端没有编译环境。

现在的做法：

```cpp
// services/gpadcservice.h
struct Action { QString id; QString name; QString description; };
typedef std::function<void (const QString &keyName, int code)> ActionHandler;

void addAction(const QString &id, const QString &name,
               const QString &description, ActionHandler handler);
void setBinding(int code, const QString &actionId);
```

服务内置两个动作，保证下拉框永远有内容、也保证「先能看见按键」：

| id | 名称 | 行为 |
|---|---|---|
| `none` | 无动作 | 只有 Debug 日志，不触发任何业务 |
| `echo` | **仅回显（默认）** | 进日志 + 页面计数，不触发任何业务 |

外部模块挂自己的动作，GPADC 一行都不用改：

```cpp
GpadcService *gpadc = GpadcService::instance();
gpadc->addAction(QStringLiteral("volume-up"), tr("音量 +"), tr("每按一次 +5"),
                 [](const QString &, int) {
    MediaService::instance()->setVolume(qMin(100, currentVolume + 5));
});
```

> 注意：`addAction()` 的 id 是唯一的。重复注册同 id 视为**覆盖**（允许模块
> 换实现），不会在下拉框里出现两项。

### 已注册的示例动作（切页）

`pages/gpadcsampleactions.*` 里注册了五个动作，作为「怎么接入」的样板：

| id | 名称 | 行为 |
|---|---|---|
| `gpadc.nav-prev` | 上一页 | `NavigationBus::requestOffset(-1)`，总览处绕回最后一页 |
| `gpadc.nav-next` | 下一页 | `NavigationBus::requestOffset(+1)`，最后一页绕回总览 |
| `gpadc.nav-home` | 回到总览 | `NavigationBus::requestHome()` |
| `gpadc.goto-wifi` | 跳到 WiFi 页 | `requestPage("wifi")`，按**页面 id** 直达 |
| `gpadc.goto-bt` | 跳到蓝牙页 | `requestPage("bt")` |

只有两个直接跳转页（WiFi / 蓝牙）是刻意的：下拉框是给用户看的，
把 12 个页面全铺进去等于把导航栏抄一遍，反而把真正常用的动作挤到后面。
要加别的页，照这两条复制、改 id 与文案即可。

### 为什么要多一个 `NavigationBus`

切页动作的实现要碰 UI，而 `services/` 不允许认识 `MainWindow`
（`docs/DESIGN-PATTERNS.md` 的依赖方向：页面不 `new QProcess`，服务不认识 `MainWindow`）。
所以中间加一条总线：

```
pages/gpadcsampleactions.cpp  ──request(offset/pageId/home)──>  core/navigationbus
                                                                      │ signal
                                                                      ▼
                                                              MainWindow
                                                        （唯一知道 QStackedWidget 的地方）
```

`MainWindow` 仍然是**唯一**认识 `QStackedWidget` 的类，它把页面 id 翻译成堆栈下标，
并在每次切页后把当前页 id 回报给总线（相对切页需要知道「现在在哪」）。

用页面 id 而不是堆栈下标：下标会随新增页面的 `order` 漂移，id 是注册时就固定的。

### 绑定持久化：为什么单独一个文件
绑定的保存路径：

| 平台 | 路径 |
|---|---|
| 板端 | `/etc/hmi-gpadc.ini` |
| 桌面 Kit | `<程序目录>/hmi-gpadc.ini` |

**没有**写进 `/etc/hmi.ini`。原因是 `/etc/hmi.ini` 是一份带大量注释的样例文件，
`QSettings` 保存时会整份重写成纯键值对，注释全丢 —— 而那份文件里的注释
（WiFi 驱动日志等级、扫描倒计时的取舍等）本身就是文档，不能被写掉。

文件内容形状：

```ini
[bindings]
K1=echo
K2=echo
K3=echo
```

键名用 `K1/K2/K3` 而不是键码：键码是硬件细节，改 dts 后会变，
而 `K1` 在用户心里是稳定标识。

---

## 五、页面行为

`pages/gpadcpage.*` 三件事：

1. **设备回显**：「查找设备」读 `/proc/bus/input/devices`；
   找不到时把实际看到的 input 设备全列出来 —— 板端最常遇到的是
   「内核没开 GPADC」或「`&gpadc` 不是 okay」，光说「没找到」没法排查。
2. **按下计数 + 最近按键**：验证「按下 / 松开」两个事件都收到了。
   只显示按下次数，不显示「按了几次、松了几次」的差值 ——
   松开事件依赖电压回升触发高阈中断，极短的点按可能只有按下没有松开，
   两个数不等是正常现象。
3. **每个键一个下拉框**：改完立刻 `setBinding()` 并落盘，同时刷新状态行。
   下拉框用 `QSignalBlocker` 包住重填动作表的过程，
   否则每 `addItem` 一项都会被当成「用户改了绑定」，把绑定冲成第一项。

「开始监听」才真正 `open()` + `QSocketNotifier`；
「刷新状态栏」/ 进入页面只做轻量探测（`refresh()`），不抢着打开设备。
这样即使绑定了错误的动作，也不会在用户没进页面时就占着 `eventN`。

唯一的例外是 `onEnter()`：进页面时若已经查到真实的 `eventN`，就自动开始监听。
按键属于「随时想按就按」的输入，要求用户先按一次「开始监听」才有反应没有必要。
桌面 Kit 上 `findEventNode()` 返回空，因此不会自动启动，也不会每次进页面刷一条告警。

---

## 六、上板验证步骤

```bash
# 1) 确认设备存在、名字对不对
cat /proc/bus/input/devices | grep -A5 -i gpadc

# 2) 确认节点在动（按一下 K1/K2/K3，应看到 type=1 code=0x73/0x77/0x175 value=1）
hexdump -C /dev/input/event2

# 3) 应用侧
/etc/init.d/S70launcher stop
/usr/bin/QtApplication --windowed    # 或直接看屏

# 4) 不接硬件也能验「按键 → 动作 → 切页」整条链路（桌面或板端都行）
QT_QPA_PLATFORM=offscreen ./QtApplication --windowed --gpadc-selftest; echo "exit=$?"
```

`--gpadc-selftest` 用 `simulateKey()` 注入与硬件**完全相同**的事件，走同一段
`handleCode()` / `dispatch()` 代码，因此它验的就是真机路径的派发逻辑。
自检会临时改写绑定来测各条路径，结束时恢复原值（不覆盖用户已有配置）。

页面上的期望现象：

| 操作 | 期望 |
|---|---|
| 进页面 | 「设备：/dev/input/event2（监听中）」（已自动开始监听） |
| 按 K1（默认绑「上一页」） | 页面切到上一页，日志「GPADC K1 按下（0x73 (115)）」 |
| 按 K2（默认绑「下一页」） | 切到下一页，计数 +1 |
| 按 K3（默认绑「回到总览」） | 直接回总览 |
| 松开 K1 | 日志 Debug「GPADC K1 松开」 |
| 下拉框改成别的动作 | 日志「GPADC K1 绑定：上一页 → xxx」，重启程序后仍是该绑定 |
| 绑成「无动作」 | 除了计数与日志，不做任何事 |

> 默认绑定是 `echo`（仅回显），**不是**切页。这样刚刷完固件时按键不会莫名其妙
> 把页面切走；要用切页在页面上选一次，绑定会落盘保存。

排查表：

| 现象 | 原因 |
|---|---|
| 「未找到 gpadc」且设备列表为空 | `/proc/bus/input/devices` 读不出来：权限或内核没开 `CONFIG_SUNXI_GPADC` |
| 「未找到 gpadc」但能看到 `sunxi-keyboard` | `board.dts` 的 `&gpadc status` 不是 `okay`，或该板没接分压按键 |
| 打开失败 `Permission denied` | 程序不是 root 跑（本工程由 init.d 以 root 拉起） |
| 按下没反应 | 先 `hexdump` 确认内核有上报；内核有、页面没有 = 命名匹配又出问题 |
| 只有按下没有松开 | 正常。松开依赖电压回升触发高阈中断，短按可能丢 |
| 按键会误报/连击 | 硬件抖动：把 `sunxi_gpadc.c` 的 `filter_cnt` 调大（文档 FAQ 5.2.1） |

---

## 七、以后再扩动作时的清单

1. 仿照 `pages/gpadcsampleactions.cpp`，写一个 `registerXxxActions()`，
   内部调 `GpadcService::instance()->addAction(id, 名称, 说明, 处理器)`；
2. 在自己模块初始化时调一次（例如对应页面的构造或 `buildContent()`）。
   **不要**改 `gpadcservice.*` 去加分支，也不要把动作实现塞进 `services/` ——
   服务只提供机制，动作属于使用方；
3. 动作 id 用小写点分或连字符（`volume-up` / `media.play-pause`），
   别和 `none` / `echo` 撞。`addAction()` 同 id 视为覆盖，所以重复注册是安全的；
4. 需要切页就用 `NavigationBus`，不要直接抓 `MainWindow`；
5. 不需要改 `gpadcpage.*` —— 下拉框在 `onEnter()` 重填，新动作自动出现；
6. 若动作在模块启动前就被用户选中过（ini 里已存该 id），
   页面会把它如实显示成 `(id)` 而不是静默落回第一项，日志也会提示「没有处理器」。

### 给蓝牙模块接入时的提示

蓝牙那边的动作（例如「切到蓝牙页并开始扫描」）等他们功能测试完再自己加。
接的时候注意两点：

- **不要在 `services/btservice.cpp` 里 include 页面或 `MainWindow`**，
  用 `NavigationBus::requestPage("bt")` 即可；
- 如果动作需要「等某个状态就绪再执行」，把逻辑留在 `Btservice` 内部，
  GPADC 这边只负责触发一次，不要在动作处理器里阻塞等待。
