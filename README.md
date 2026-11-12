# HMI-T113S4

全志 T113-S4 板载 HMI 主程序（**当前只做桌面版**）。**C++11 + qmake**，界面与交互在 Windows 上开发与验证，源码结构仍保留移植到板端的余地。
当前交付为 **M1：软件架构一次定死 + 可编译可运行的骨架 + 进镜像 + 自启链路**；M2–M5 的页面与服务接口全部就位，功能按里程碑增量填充。

---

## 一、目录结构（一次定死）

```
HMI-T113S4/
├── HMI-T113S4.pro          QT += core gui widgets multimedia network serialport；TARGET 保留
├── main.cpp                翻译 / 全局字体 / 单实例 / AppSettings(/etc/hmi.ini) / 执行器注入
├── mainwindow.*            左导航 QListWidget 168px 11 项 + QStackedWidget + 状态栏
├── core/                   设计模式基础设施（单例/命令/状态总线/注册表/配置）
│   ├── singleton.h            单例模板
│   ├── commandspec.*          建造者：一条命令的 5 个维度
│   ├── commandrunner.*        策略：命令执行抽象
│   ├── processcommandrunner.* 适配器：QProcess 异步 → submit/finished
│   ├── loggingcommandrunner.* 装饰器：统一日志 + 计时
│   ├── simulatedcommandrunner.* 空对象：桌面预置输出
│   ├── command.*              命令 + 组合（ShellCommand / CommandSequence）
│   ├── commandinvoker.*       调用者：串行队列
│   ├── devicestatus.h         状态值对象；statetable.* 状态转移表
│   ├── statusbus.*            观察者 + 中介者：设备状态总线
│   ├── ideviceservice.h       服务抽象；deviceservice.* 服务模板
│   ├── serviceregistry.*      服务注册表 + 轮询调度
│   ├── pagefactory.*          页面工厂 + 自注册表
│   ├── playerstate.*          状态（多态）：播放器四态
│   ├── qrcode.*               二维码编码器（纯 C++，字节模式/L/V1–V10）
│   ├── httpserver.*           极简 HTTP/1.1 服务端（QTcpServer，流式上传下载）
│   └── appsettings.*          单例 + 外观：ini 读取与钳制
├── models/                 wifiscanmodel.*（QAbstractListModel：`wpa_cli scan_results` 文本 → 可点列表）
├── pages/                  basepage(模板方法) + homepage wifipage btpage mediapage i2cpage
│                           rs485page canpage gpadcpage ethpage adbpage  (10 个页面)
├── services/               processrunner(外观) + wifiservice btservice ethservice adbservice
│                           i2cservice rs485service gpadcservice mediaservice
│                           fileservice(路径沙箱) webshareservice(网页管理编排)
├── widgets/                uimetrics.h(尺寸唯一来源) LogPanel CardButton StatusDot QrCodeWidget
├── resources/              qss/style.qss、i18n/*.ts、web/(手机端网页) → 构建期产出 *.qm
├── docs/DESIGN-PATTERNS.md 每个设计模式「在哪 / 为什么 / 替代了什么」
├── docs/WEB-FILE-MANAGER.md 扫码文件管理：接口表 / 安全模型 / 与 FongMi 的对应
├── etc/hmi.ini             板端配置样例
└── tools/                  verify.py（结构自检）、gen-i18n.sh
```

> **导航项数不再靠约定维持**：每个页面在文件末尾用
> `HMI_REGISTER_PAGE(id, order, QT_TR_NOOP("名称"), 类名)` 自注册，`MainWindow` 遍历注册表建页与导航；
> 只有总览页登记为「无导航文案 + order=0」，因此自然得到 **11 项导航 + 12 页**（「网页管理」order=100、「U盘升级」order=110）。
> 想让总览也进导航：把 `pages/homepage.cpp` 最后一行的 `nullptr` 换成 `QT_TR_NOOP("总览")`，别处不用动。
> 导航项高 42px，超出可视高度时列表可滚动（ScrollPerPixel）。

---

## 二、硬约定落实对照

| 约定 | 落地位置 |
|---|---|
| C++11 + qmake | `HMI-T113S4.pro`：`CONFIG += c++11`，三个 `.pri` 分片 |
| 全局字体 17px、不硬编码字体族 | `main.cpp`：`font.setPixelSize()`；中文靠 rootfs `wqy-zenhei` + fontconfig |
| 单实例 | `main.cpp`：`QLocalServer` / `QLocalSocket`，重复启动唤醒已有实例 |
| `QSettings /etc/hmi.ini` | `main.cpp` 只读；`etc/hmi.ini` 提供样例；缺文件走默认值 |
| 所有外部命令经 ProcessRunner | `services/processrunner.*` 是外观；真实执行在 `core/processcommandrunner.cpp`：异步 `QProcess` + `MergedChannels` + 3–5s 超时 + 无 `waitFor*` |
| 失败把 stderr 打进 LogPanel | `core/loggingcommandrunner.cpp`（装饰器，唯一实现）：启动失败/超时/非零退出/崩溃全量输出 → `LogPanel::Error`；`qWarning` 亦转发 |
| 页面/服务可扩展不改主窗口 | `core/pagefactory.*` + `core/serviceregistry.*`：自注册表，`MainWindow` 只遍历 |
| 设备状态统一出口 | `core/statusbus.*`：服务 `publish(DeviceStatus)`，状态栏与页面 `subscribe` |
| LogPanel 只读、限 500 行、全局统一 | `widgets/logpanel.*`：`setReadOnly(true)` + `setMaximumBlockCount(500)`；`MainWindow` 持唯一实例，页面共用 |
| 触摸友好 | 尺寸集中在 `widgets/uimetrics.h`：导航 168px、项高 42px、按钮 42/58px、字号 14–18pt。42px 仍是 ≥44px 级别的可触摸目标，板端嫌小可一处调回 |
| root 直接访问设备节点 | `I2cService` `/dev/i2c-2`、`Rs485Service` `/dev/ttyS2`、`GpadcService` `/dev/input/eventN` |
| 跳线帽常驻提示 | RS485 页、以太网页、首页均常驻「跳线帽：左 RS485 / 右 以太网」 |
| 二进制不改名 | `.pro` 的 `TARGET` 保持 `HMI-T113S4`；安装 / U 盘升级时改名成 `/usr/bin/QtApplication` |

---

## 三、构建

### Windows 本机（自带 Qt 5.15.2 + MinGW 8.1，已跑通）
```bash
bash tools/setup-win-toolchain.sh    # 一次性：装到工作区 .toolchain/，不污染系统
bash tools/build-win.sh              # 出 dist/win/，整个目录可直接双击运行
```
详见 `docs/BUILD-WINDOWS.md`（含「为什么要用 ASCII 目录联接」的实测证据与排错对照表）。

### 桌面 Kit（Qt 5.15，UI 调试）
```bash
qmake HMI-T113S4.pro
make -j$(nproc)
./HMI-T113S4 --windowed            # 板端默认全屏，--windowed 便于桌面调试
./HMI-T113S4 --simulate            # 用 SimulatedCommandRunner：扫描/联网/ADB 全部有预置输出
QT_QPA_PLATFORM=offscreen ./HMI-T113S4 --selftest
```
硬件相关代码用 `Q_OS_LINUX` 包裹，桌面 Kit 会自动跳过 ioctl / input_event，不依赖板端头文件。
若 Kit 缺 `lrelease`：`qmake CONFIG+=hmi_no_lrelease HMI-T113S4.pro`（源码 `tr()` 本身即中文，不影响显示）。
`--simulate` 也可写进 `/etc/hmi.ini` 的 `[ui] simulate=true`。
Windows 下配置文件改读程序目录的 `hmi.ini`，默认窗口化，多媒体与网页共享目录默认取用户目录。

---

## 四、M1 验收清单

- [x] `python tools/verify.py` 通过：49 源文件 / 51 头 / 11 导航 / 12 页面 / 11 服务，moc 覆盖、include 闭合、qrc 与 .ts 合法、括号配对、硬约定 + 设计模式 + 网页管理检查
- [x] **Windows 本机真实编译通过**：PE32+ x86-64，`dist/win/` 双击可运行；`--selftest` 退出码 0；网页文件管理的列表/上传/下载/改名/删除/越权拦截均实测通过
- [ ] 状态栏时间 / WiFi / ETH / ADB 实时刷新；任一命令失败时 stderr 出现在底部日志

> **当前范围：Windows 桌面版**。已真实编译运行（见 `docs/BUILD-WINDOWS.md`）；
> 硬件相关页面在 PC 上只会输出「命令不存在」的日志，属预期——加 `--simulate` 可看预置输出。
> ARM 交叉编译与上板部署不在本次范围内。

---

## 五、局域网网页文件管理（扫码打开，详见 docs/WEB-FILE-MANAGER.md）

手机扫设备屏幕上的二维码 → 打开网页 → 浏览 / 上传 / 下载 / 重命名 / 删除设备文件。

| 环节 | 实现 |
|---|---|
| HTTP 服务 | `core/httpserver.*`：QTcpServer 上的极简 HTTP/1.1，零第三方依赖，`Connection: close` |
| 业务编排 | `services/webshareservice.*`：路由 + JSON + 上传流落盘；端口被占时从 8080 顺延 10 个 |
| 路径沙箱 | `services/fileservice.*`：白名单 + `canonicalFilePath` 校验，根目录禁止删除/改名 |
| 网页 | `resources/web/{index.html,app.css,app.js}`，内嵌到 qrc → `:/web/`，手机端自适应 |
| 二维码 | `core/qrcode.*` 自己算 + `widgets/qrcodewidget` 绘制（4 模块静区、整数倍缩放） |
| 页面 | `pages/websharepage`（导航第 10 项）：开关服务、显示地址/二维码/连接数/最近请求 |
| U 盘升级 | `pages/upgradepage`（导航第 11 项）：检测 U 盘 → 校验包 → 覆盖主程序/翻译/配置 → 提示重启；引擎见 `services/upgradeservice`，包格式与安全设计见 `docs/UPGRADE.md` |

配置（`/etc/hmi.ini`）：`[web] port=8080 / token= / roots=/usr,/tmp,/var,/opt,/media / autostart=true`。
二维码验证：`python tools/qr_verify.py build/qr_selftest.exe`（63 组，全部解码还原）。

---

## 六、设计模式怎么落的（详见 docs/DESIGN-PATTERNS.md）

| 层 | 模式 | 一眼看懂的位置 |
|---|---|---|
| 进程层 | 外观 · 策略 · 装饰器 · 适配器 · 建造者 · 空对象 | `services/processrunner.*` → `core/{commandrunner, loggingcommandrunner, processcommandrunner, commandspec, simulatedcommandrunner}` |
| 动作层 | 命令 · 组合 · 调用者 | `core/command.*` + `core/commandinvoker.*`；用例：`WifiService::makeConnectSequence()`、`EthService::makeConnectSequence()`、首页「环境自检」 |
| 状态层 | 观察者 · 中介者 · 状态机 · 状态（多态） | `core/statusbus.*` + `core/statetable.*`；`core/playerstate.*` + `MediaService` |
| 结构层 | 单例 · 工厂 · 注册表 · 服务定位器 · 模板方法 · 依赖倒置 | `core/singleton.h`、`core/pagefactory.*`、`core/serviceregistry.*`、`pages/basepage.*`、`core/ideviceservice.h` |
| 数据层 | Model/View（MVC） | `models/wifiscanmodel.*` + WiFi 页 `QListView` |

三条扩展路径都是「加文件 + 一行注册」，不用动 `MainWindow`：
`HMI_REGISTER_PAGE("id", order, QT_TR_NOOP("名"), 类)` / `HMI_REGISTER_SERVICE("id", 类)` / `ProcessRunner::installBackend(...)`。

---

## 七、后续里程碑的开关（架构已预留，不返工）

| 里程碑 | 已就位 | 待接入 |
|---|---|---|
| M2 WiFi / ETH / ADB | WiFi 链路（驱动 + wlan0 + `wifi_daemon`）由 `/etc/init.d/S41wifi` 开机拉起，应用做 `wifi -o sta/-c/-d`、扫描用 `wpa_cli -p <socket 目录> -i wlan0`（回退 `iw`）；ETH/ADB 走 `ifconfig`/`udhcpc`/`ping`、`S50adb_start`；状态栏由 `StatusBus` 广播，`ServiceRegistry` 按各自间隔轮询（WiFi/ETH/ADB 5s） | 板端实测 |
| M3 I2C / RS485 / GPADC | `I2cService` 只读扫描实现、`Rs485Service` QSerialPort 收发、`GpadcService` 找 eventN + QSocketNotifier + 键码映射 | 指定地址读寄存器、全地址扫描移入工作线程、跳线帽实测 |
| M4 多媒体 | `MediaService` 播放/音量/错误日志、MediaPage 控件与「软解」标注 | 追加 `QT += multimediawidgets` 后把 `QVideoWidget` 绑到 `MediaService::player()` |
| M5 CAN | CanPage 设备发现/位速率/up/loopback/cansend 命令链；内核开关说明写在页面提示 | 改 `config-5.4` 开 `CONFIG_CAN/CAN_RAW/CAN_DEV/AWLINK_SUN8I`，再 `qmake CONFIG+=hmi_can`（自动 `QT += serialbus` + `HMI_HAVE_CAN`），补 QCanBus 收发 |
| M6 打磨 | 首页内存/负载/温度、页面 `onEnter/onLeave` 钩子、配置化字体与日志行数；网页管理已可用 | watchdog、页面按需加载、启动 <3s、网页端预览播放 |

---

## 八、教学点

1. **原子替换**：升级时先写 `<目标>.new`、备份 `.bak`、再 `rename()`。`rename` 是原子的，且 Linux 允许替换正在运行的二进制——直接 `cp` 覆盖会 `ETXTBSY`。
2. **路径沙箱**：网页文件管理先把路径转成 canonical，再与白名单前缀比对；跨平台判定要用 `QDir::isAbsolutePath()`，`startsWith('/')` 在 Windows 上会把所有路径判成非法。
3. **信号与状态的职责分离**：导航跳转绑 `itemClicked`（用户动作），不绑 `currentRowChanged`（列表内部状态）——后者在"切走再点回同一项"时根本不发信号。
4. **可回归的 UI**：`--page N --shot out.png` 用 `QWidget::grab()` 离屏抓图；`--upgrade-selftest` 无人值守跑完整升级链路，退出码即结论。
