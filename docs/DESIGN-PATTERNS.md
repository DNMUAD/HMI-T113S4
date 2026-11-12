# HMI-T113S4 设计模式落地说明

> 目标：不是「为了用模式而用模式」，而是把 M1–M5 里**真实存在的坏味道**按模式拆掉。
> 每一条都给出「在哪、为什么、替代了什么」，并保证代码可编译、可回归（`python tools/verify.py`）。

---

## 0. 一次看清分层

```
┌──────────────────────────────────────────────────────────────┐
│ pages/     10 个页面      ← 只搭界面 + 转译用户动作            │
│            BasePage（模板方法）  PageFactory（工厂+注册表）     │
├──────────────────────────────────────────────────────────────┤
│ services/  8 个硬件服务   ← 只懂业务命令与解析                  │
│            DeviceService（服务模板）  ProcessRunner（外观）     │
├──────────────────────────────────────────────────────────────┤
│ core/      基础设施       ← 命令、状态、注册表、配置            │
│  CommandSpec    建造者        StatusBus        观察者+中介者     │
│  CommandRunner  策略          StateTransitionTable  状态机       │
│  Logging...     装饰器        PlayerState      状态（多态）      │
│  Simulated...   空对象        PageFactory      工厂+注册表       │
│  Command/Sequence 命令+组合   ServiceRegistry  服务定位器        │
│  CommandInvoker 调用者        AppSettings      单例+外观         │
├──────────────────────────────────────────────────────────────┤
│ models/    WifiScanModel  ← Model/View（MVC 的 Qt 形态）        │
│ widgets/   LogPanel / CardButton / StatusDot                    │
└──────────────────────────────────────────────────────────────┘
```

依赖方向永远向下：页面不 `new QProcess`，服务不认识 `MainWindow`，`core/` 不认识具体硬件。

---

## 1. 模式总表

| # | 模式 | 落点 | 一句话作用 |
|---|------|------|-----------|
| 1 | 单例 Singleton | `core/singleton.h`、`AppSettings`、`ProcessRunner`、8 个服务 | 全局唯一入口，且只写一次「懒加载」 |
| 2 | 外观 Facade | `services/processrunner.*`、`services/*service.*` | 把 QProcess / shell / sysfs / ioctl 挡在一层之后 |
| 3 | 策略 Strategy | `core/commandrunner.h`（真实 / 模拟 / 装饰） | 换执行方式不改调用方 |
| 4 | 装饰器 Decorator | `core/loggingcommandrunner.*` | 日志与计时**只写一处**，套在任意执行器外 |
| 5 | 建造者 Builder | `core/commandspec.*` | 五维命令用链式拼装，不传四个位置参数 |
| 6 | 命令 Command | `core/command.*`（`Command` / `ShellCommand`） | 把硬件动作变成可排队、可组合、可取消的对象 |
| 7 | 组合 Composite | `core/command.*`（`CommandSequence`） | 一串命令当一条命令用，失败即中断 |
| 8 | 调用者 Invoker | `core/commandinvoker.*` | 串行队列：同一时刻只允许一组硬件动作 |
| 9 | 观察者 Observer | `core/statusbus.*` + Qt 信号槽 | 服务 publish，状态栏/页面 subscribe |
| 10 | 中介者 Mediator | `core/statusbus.*` | 服务与窗口互不认识，只认总线 |
| 11 | 状态机 State（表驱动） | `core/statetable.*` + `StatusBus` | 拦截非法跳变与重复上报 |
| 12 | 状态 State（多态） | `core/playerstate.*` + `MediaService` | 播放器行为随状态变，消灭 if 链 |
| 13 | 工厂方法 Factory | `core/pagefactory.*` | 页面由注册表创建 |
| 14 | 注册表 / 服务定位器 Registry | `core/pagefactory.*`、`core/serviceregistry.*` | 自注册 + 懒加载 + 按需轮询 |
| 15 | 模板方法 Template Method | `pages/basepage.*`、`core/deviceservice.*` | 父类定骨架，子类只填内容 |
| 16 | 适配器 Adapter | `core/processcommandrunner.*`、`WifiScanModel` | QProcess / 原始文本 → 统一接口 |
| 17 | 空对象 Null Object | `core/simulatedcommandrunner.*` | 桌面无硬件也能跑通整条链路 |
| 18 | 依赖倒置 / 接口隔离 | `core/ideviceservice.h` | 上层只依赖 4 个方法的抽象 |
| 19 | Model/View（MVC） | `models/wifiscanmodel.*` + `QListView` | 解析规则与显示分离 |

---

## 2. 逐条解说（在哪 · 为什么 · 替代了什么）

### ① 单例 Singleton —— `core/singleton.h`

```cpp
template <typename T, typename Factory>
T *singleton(Factory factory)
{
    static T *instance = nullptr;
    if (!instance)
        instance = factory();
    return instance;
}
```

全工程 15 个 `instance()` 收敛到这一个模板，类的构造函数保持 `private`：

```cpp
WifiService *WifiService::instance()
{
    return hmi::singleton<WifiService>([]() { return new WifiService; });
}
```

- **为什么**：`static 指针 + 判空 + new` 的复制粘贴原本在 10 个类里重复，改钳制/改线程策略要改 10 处。
- **替代了什么**：`static XxxService *s = nullptr; if (!s) s = new ...;` 的手工单例。
- **边界**：C++11 的函数内 `static` 初始化本身线程安全，但 `if (!instance)` 不是原子的。本机是单线程 GUI（init.d 拉起、一个主线程），够用；要跨线程首访，只改这一个模板即可，调用方零改动。

### ② 外观 Facade —— `ProcessRunner`

硬约定「所有外部命令必须经此层」的实现：

```
services ──> ProcessRunner（外观）──> LoggingCommandRunner（装饰）
                                        └─> ProcessCommandRunner（QProcess 适配）
```

对外仍是 `run(program, args, timeout, stdin)`，内部退化成「组一个 `CommandSpec` 交给策略执行器」。

- **为什么**：服务层不该知道 QProcess 的生命周期，更不该出现 `waitForFinished`。
- **替代了什么**：每个服务里各写一遍 `QProcess` + 信号 + 超时定时器。

### ③ 策略 Strategy —— `CommandRunner`

```cpp
class CommandRunner : public QObject {          // 抽象策略
    virtual int  submit(const CommandSpec &spec) = 0;
    virtual void cancel(int jobId) = 0;
signals:
    void finished(int jobId, int exitCode, int exitStatus, const QString &out);
};
```

三个实现：`ProcessCommandRunner`（板端真跑）、`SimulatedCommandRunner`（桌面预置输出）、`LoggingCommandRunner`（装饰另一个策略）。运行期注入：

```bash
./HMI-T113S4 --simulate        # 桌面演示：扫描/连网/ADB 全有假数据
./HMI-T113S4                   # 板端：真起 QProcess
```

- **为什么**：桌面没有 `wifi` / `udhcpc` / `/dev/i2c-2`，过去只能靠 `#ifdef` 或「点了就报错」。
- **替代了什么**：调用点里的 `#ifdef Q_OS_LINUX` 分支。

### ④ 装饰器 Decorator —— `LoggingCommandRunner`

```cpp
m_backend = new LoggingCommandRunner(new ProcessCommandRunner);
```

它只做两件事：`submit()` 时记 `$ 命令`，`finished()` 时按退出码打 Info/Error 并附耗时。

- **为什么**：「失败必须把 stderr 完整打进 LogPanel」是硬约定，必须只有一处实现。
- **替代了什么**：把日志散落在 `ProcessRunner`、各服务、各页面里，格式各不相同。
- **顺带**：`CommandSpec::quiet()` 让「预期失败的探测」（如 `pidof adbd`）降到 Debug，避免每 5 秒刷一条 Error。

### ⑤ 建造者 Builder —— `CommandSpec`

```cpp
CommandSpec(QStringLiteral("wifi"))
    .arg("-c").arg(ssid).arg(password)
    .timeout(4000)
    .describe(tr("连接 %1").arg(ssid))
    .quiet();
```

- **为什么**：`run(prog, args, timeout, stdin)` 四个位置参数极易传错；参数化传参天然免疫 shell 注入。
- **替代了什么**：手工拼接 `"wifi -c " + ssid + " " + pwd` 这种字符串（拼错即命令注入）。

### ⑥⑦⑧ 命令 + 组合 + 调用者 —— `core/command.*`、`core/commandinvoker.*`

```cpp
// 服务侧：业务知识 → 命令对象（EthService::makeConnectSequence）
CommandSequence *EthService::makeConnectSequence()
{
    CommandSequence *seq = new CommandSequence(tr("以太网一键联网"));
    seq->add(new ShellCommand(CommandSpec("ifconfig").arg("eth0").arg("up"), tr("网卡 UP")))
       ->add(new ShellCommand(CommandSpec("udhcpc").arg("-i").arg("eth0"), tr("DHCP")))
       ->add(new ShellCommand(CommandSpec("ping").arg("-c").arg("1").arg("www.baidu.com"), tr("连通性验证")));
    return seq;
}

// 页面侧：只负责「下命令」
CommandInvoker::instance()->enqueue(EthService::instance()->makeConnectSequence());
```

- **Command**：动作变成对象 → 可描述、可排队、可取消、可复用。
- **Composite**：`CommandSequence` 对外和单条命令一样，内部顺序执行，任一步失败即中断并报「第 N 步失败」。
- **Invoker**：`QQueue` 串行化，同一时刻只允许一组硬件动作在跑 —— 顺手解决「用户连点三个按钮把 `/dev/i2c-2` 打爆」。
- **替代了什么**：页面里 `job1 = run(...)`、在 `finished` 里判断 `if (id == job1) job2 = run(...)` 的回调地狱。

### ⑨⑩ 观察者 + 中介者 —— `StatusBus`

```cpp
// 服务侧（DeviceService::publish 的底层）
StatusBus::instance()->publish(DeviceStatus("wifi", DeviceReady, "192.168.1.23"));

// 订阅侧（状态栏 / 页面）
connect(StatusBus::instance(), &StatusBus::changed, this, &MainWindow::onStatusChanged);
```

- **Observer**：一对多广播，订阅者互不知道对方。
- **Mediator**：`WifiService` 不认识 `MainWindow`，`MainWindow` 也不认识 `WifiService`；新增「CAN 状态灯」只需服务 publish + 状态栏加一个 `StatusDot`。
- **替代了什么**：`MainWindow::connectServices()` 里 3 组硬编码 connect，以及每个页面各自 connect 一遍服务。

### ⑪ 状态机（State 的表驱动变体）—— `StateTransitionTable`

```cpp
const bool kAllowed[5][5] = {
    // → Unknown  Off    Busy   Ready  Error
    {   true,    true,  true,  true,  true },   // Unknown
    {   true,    false, true,  true,  true },   // Off
    ...
};
```

`StatusBus::publish()` 先查表再广播：

- 完全相同的 `(state, detail)` → 静默丢弃（状态栏不闪、日志不刷屏）；
- 状态迁移不在白名单 → 记一条「忽略非法状态迁移」，不广播。

- **为什么**：轮询式状态上报必然重复，`Ready→Ready` 会把状态灯刷成频闪。
- **替代了什么**：状态栏里 `if (ip != lastIp) setText(...)` 这类散落的去重代码。
- **为什么不用多态 State**：这里状态少、规则是数据，表驱动更短、可在 ini 里配置；真要换成多态，接口不变。

### ⑫ 状态 State（多态版）—— `PlayerState` + `MediaService`

```cpp
void MediaService::play() { m_state->play(*this); }   // 上下文不判断状态

bool IdleState::play(MediaService &ctx)               // 状态自己决定
{
    if (!ctx.hasMedia()) { ctx.notify(tr("请先选择媒体文件")); return false; }
    ctx.player()->play();
    ctx.setState(PlayingState::instance());
    return true;
}
```

四个状态：`Idle / Playing / Paused / Error`。

- **为什么**：播放器在「空闲时按暂停」「错误态按播放」下的正确行为完全不同，写在 `MediaService` 里就是 4×3 的 if 矩阵。
- **替代了什么**：`if (m_state == "播放中") ... else if (m_state == "暂停") ...` 的字符串状态比较。
- **可见效果**：没选文件就点暂停，日志与状态行会给出「当前没有正在播放的内容」，而不是把空 URL 丢给 gstreamer。

### ⑬⑭ 工厂 + 注册表 —— `PageFactory`、`ServiceRegistry`

页面在自己文件末尾自注册：

```cpp
HMI_REGISTER_PAGE("wifi", 10, QT_TR_NOOP("WiFi"), WifiPage)
```

`MainWindow::buildPages()` 只遍历注册表：

```cpp
const QList<PageFactory::Entry> entries = PageFactory::instance()->entries();
for (...) { BasePage *p = PageFactory::instance()->create(entry.id, this); ... }
```

服务同理：

```cpp
HMI_REGISTER_SERVICE("wifi", WifiService)
```

`ServiceRegistry` 负责：懒加载（`start()` 时才建对象）、按各自 `pollIntervalMs()` 调度（WiFi 5s / 蓝牙 15s / 总线类不轮询）、`refreshAll()` 一键全刷。

- **为什么**：`MainWindow` 里 10 个 `new XxxPage` + 9 行导航文案 + 3 个服务的 5 秒定时器，是全工程最典型的变化点。
- **替代了什么**：
  - 加页面要改 `MainWindow`（违反开闭原则）；
  - 状态栏写死 5 秒刷新，蓝牙这种慢设备也被拖着一起查。
- **注意**：导航文案存**未翻译源串**（`QT_TR_NOOP`），在 `entries()` 时才 `translate()` —— 因为静态初始化发生在翻译装载之前。

### ⑮ 模板方法 Template Method —— `BasePage`、`DeviceService`

```cpp
// 父类定次序
void BasePage::initialize()
{
    if (m_initialized) return;
    m_initialized = true;
    buildContent();          // 子类钩子（对象已完整构造，虚函数才正确派发）
}

// 子类只填内容
void WifiPage::buildContent() { /* 控件 + 信号绑定 */ }
```

`DeviceService` 同理：登记注册表 → 发布状态 → 提供 `deviceId()/displayName()` 门面，子类只实现 `refresh()` 与业务动作。

- **为什么**：之前页面在构造函数里铺控件，一旦有人在构造函数中调用虚函数就会踩「派生部分尚未构造」的坑；现在统一由 `PageFactory::create()` 调 `initialize()`。
- **替代了什么**：10 个页面各写一遍「骨架 + 内容」的构造顺序。

### ⑯ 适配器 Adapter —— `ProcessCommandRunner`、`WifiScanModel`

- `ProcessCommandRunner`：把 QProcess 的 `readyRead / finished / errorOccurred + QTimer` 适配成 `submit() → finished(jobId, code, out)`。
- `WifiScanModel`：把 `wpa_cli scan_results` / `iw ... scan` 的设备相关文本适配成 `QAbstractListModel` 的行（还顺手做了去重与按信号排序）。

### ⑰ 空对象 Null Object —— `SimulatedCommandRunner`

桌面无硬件时返回预置输出并照常发 `finished`，于是「页面 → 服务 → 执行器」全链路可跑。

- **为什么**：UI 布局、导航、日志、状态灯都需要在 PC 上验证，不能靠板子。
- **替代了什么**：调用点里的 `#ifdef` / `if (qEnvironmentVariableIsSet(...))` 分支。

### ⑱ 依赖倒置 / 接口隔离 —— `IDeviceService`

```cpp
class IDeviceService {
    virtual QString deviceId() const = 0;
    virtual int  pollIntervalMs() const = 0;
    virtual void refresh() = 0;
};
```

上层（注册表、自检、状态栏）只依赖这 4 个方法，不依赖 `WifiService` 具体类。

### ⑲ Model/View（MVC）—— `WifiScanModel` + `QListView`

| 角色 | 对应 |
|------|------|
| Model | `WifiScanModel`（解析、去重、排序、Role） |
| View | `QListView`（显示与选中） |
| Controller（薄） | `WifiPage`（把 `activated` 转成「填 SSID / 去连接」） |

- **替代了什么**：把扫描原文塞进 `QPlainTextEdit` —— 能看不能用，换个芯片就得重写解析。

---

## 3. 三个「照着做」的扩展动作

### 新增一个页面（3 步，零改 MainWindow）

1. `pages/xxxpage.h/.cpp`：继承 `BasePage`，实现 `buildContent()`；
2. 文件末尾 `HMI_REGISTER_PAGE("xxx", 100, QT_TR_NOOP("XXX"), XxxPage)`；
3. `pages/pages.pri` 加两行（HEADERS / SOURCES）。

导航项、`QStackedWidget` 顺序、`onEnter/onLeave` 自动生效。

### 新增一个设备服务（3 步，零改状态栏）

1. 继承 `DeviceService`，构造时给 `(deviceId, 显示名, 轮询间隔)`；
2. 在探测结果处 `publish(DeviceReady/Off/Busy/Error, 详情)`；
3. 文件末尾 `HMI_REGISTER_SERVICE("xxx", XxxService)` + `services/services.pri` 两行。

要状态灯就再 `m_dots.insert("xxx", new StatusDot(...))`。

### 换一个命令执行器（1 行）

```cpp
ProcessRunner::instance()->installBackend(new LoggingCommandRunner(new MyRunner));
CommandInvoker::instance()->setRunner(ProcessRunner::instance()->backend());
```

---

## 4. 刻意没用的模式

| 模式 | 为什么不用 |
|------|-----------|
| 抽象工厂 | 只有两类产品（执行器、页面），简单工厂 + 注册表已够；再加一层是纯开销 |
| 备忘录 Memento | 没有「撤销」需求；HMI 的硬件动作不可逆，撤销语义是假的 |
| 访问者 Visitor | 没有稳定的对象结构 + 多变操作，加它只会让接口更难读 |
| 代理 Proxy | QProcess 的懒启动由策略层就解决了，不需要额外代理类 |
| 全局单例满天飞 | 只有真正的全局资源（日志、配置、执行器、总线、注册表）做单例，页面/命令一律普通对象 |

---

## 5. 回归校验

```bash
python tools/verify.py
```

脚本除原有「文件齐备 / moc 覆盖 / include 闭合 / 硬约定」外，新增：

- 每个 `pages/*page.cpp` 必须 `HMI_REGISTER_PAGE(...)`，id 与 order 不得重复，总览页必须唯一且 `order=0`；
- 每个 `services/*service.cpp` 必须 `HMI_REGISTER_SERVICE(...)`；
- `new QProcess` 只允许出现在 `core/processcommandrunner.cpp`；`waitForFinished/waitForStarted/system()/QThread::sleep` 全工程禁止；
- 失败输出必须由 `LoggingCommandRunner` 以 `LogPanel::Error` 记录；
- `MainWindow` 必须通过 `PageFactory` / `StatusBus` 解耦；`BasePage` 必须有 `buildContent()` 钩子。
