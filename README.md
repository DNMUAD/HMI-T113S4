# HMI-T113S4

全志 T113-S4 板载 HMI 主程序。**C++11 + qmake**，与 SDK 内 `qtapp` / `qt-launcher` 保持一致，便于 buildroot 集成。
当前交付为 **M1：软件架构一次定死 + 可编译可运行的骨架 + 进镜像 + 自启链路**；M2–M5 的页面与服务接口全部就位，功能按里程碑增量填充。

---

## 一、目录结构（一次定死）

```
HMI-T113S4/
├── HMI-T113S4.pro          QT += core gui widgets multimedia network serialport；TARGET 保留
├── main.cpp                翻译 / 全局字体 / 单实例 / AppSettings(/etc/hmi.ini) / 执行器注入
├── mainwindow.*            左导航 QListWidget 180px 9 项 + QStackedWidget 844x600 + 状态栏
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
│   └── appsettings.*          单例 + 外观：ini 读取与钳制
├── models/                 wifiscanmodel.*（QAbstractListModel：`wifi -s` 文本 → 可点列表）
├── pages/                  basepage(模板方法) + homepage wifipage btpage mediapage i2cpage
│                           rs485page canpage gpadcpage ethpage adbpage  (10 个页面)
├── services/               processrunner(外观) + wifiservice btservice ethservice adbservice
│                           i2cservice rs485service gpadcservice mediaservice
├── widgets/                LogPanel(只读/500 行) CardButton(≥56px) StatusDot
├── resources/              qss/style.qss、i18n/*.ts → 构建期产出 *.qm、图标
├── docs/DESIGN-PATTERNS.md 每个设计模式「在哪 / 为什么 / 替代了什么」
├── etc/hmi.ini             板端配置样例
├── buildroot/config/...    Config.in + hmi-t113s4.mk + auto/Config.in.append（包三件套）
├── notes/hmi-t113s4/       sync-src.sh、install-buildroot.sh
├── deploy/adb-deploy.sh    Qt Creator 自定义 Deploy 步骤
└── tools/                  verify.py（结构自检）、gen-i18n.sh
```

> **「9 项导航」与「10 个页面」不再靠约定维持**：每个页面在文件末尾用
> `HMI_REGISTER_PAGE(id, order, QT_TR_NOOP("名称"), 类名)` 自注册，`MainWindow` 遍历注册表建页与导航；
> 只有总览页登记为「无导航文案 + order=0」，因此自然得到 9 项导航 + 10 页。
> 想改成 10 项导航：把 `pages/homepage.cpp` 最后一行的 `nullptr` 换成 `QT_TR_NOOP("总览")` 即可，别处不用动。

---

## 二、硬约定落实对照

| 约定 | 落地位置 |
|---|---|
| C++11 + qmake | `HMI-T113S4.pro`：`CONFIG += c++11`，三个 `.pri` 分片 |
| 全局字体 16pt、不硬编码字体族 | `main.cpp`：`font.setPointSize()`；中文靠 rootfs `wqy-zenhei` + fontconfig |
| 单实例 | `main.cpp`：`QLocalServer` / `QLocalSocket`，重复启动唤醒已有实例 |
| `QSettings /etc/hmi.ini` | `main.cpp` 只读；`etc/hmi.ini` 提供样例；缺文件走默认值 |
| 所有外部命令经 ProcessRunner | `services/processrunner.*` 是外观；真实执行在 `core/processcommandrunner.cpp`：异步 `QProcess` + `MergedChannels` + 3–5s 超时 + 无 `waitFor*` |
| 失败把 stderr 打进 LogPanel | `core/loggingcommandrunner.cpp`（装饰器，唯一实现）：启动失败/超时/非零退出/崩溃全量输出 → `LogPanel::Error`；`qWarning` 亦转发 |
| 页面/服务可扩展不改主窗口 | `core/pagefactory.*` + `core/serviceregistry.*`：自注册表，`MainWindow` 只遍历 |
| 设备状态统一出口 | `core/statusbus.*`：服务 `publish(DeviceStatus)`，状态栏与页面 `subscribe` |
| LogPanel 只读、限 500 行、全局统一 | `widgets/logpanel.*`：`setReadOnly(true)` + `setMaximumBlockCount(500)`；`MainWindow` 持唯一实例，页面共用 |
| 触摸友好 | `CardButton` 最小高 56px、导航项 56px、输入控件 56px、字号 16–20pt |
| root 直接访问设备节点 | `I2cService` `/dev/i2c-2`、`Rs485Service` `/dev/ttyS2`、`GpadcService` `/dev/input/eventN` |
| 跳线帽常驻提示 | RS485 页、以太网页、首页均常驻「跳线帽：左 RS485 / 右 以太网」 |
| 不改 S70launcher | `hmi-t113s4.mk` 安装行把二进制装成 `/usr/bin/QtApplication` |

---

## 三、构建

### 桌面 Kit（Qt 5.15，UI 调试）
```bash
qmake HMI-T113S4.pro
make -j$(nproc)
./HMI-T113S4 --windowed            # 默认全屏，--windowed 便于桌面调试
./HMI-T113S4 --simulate            # 用 SimulatedCommandRunner：扫描/联网/ADB 全部有预置输出
QT_QPA_PLATFORM=offscreen ./HMI-T113S4 --selftest
```
硬件相关代码用 `Q_OS_LINUX` 包裹，桌面 Kit 会自动跳过 ioctl / input_event，不依赖板端头文件。
若 Kit 缺 `lrelease`：`qmake CONFIG+=hmi_no_lrelease HMI-T113S4.pro`（源码 `tr()` 本身即中文，不影响显示）。
`--simulate` 也可写进 `/etc/hmi.ini` 的 `[ui] simulate=true`。

### 交叉 Kit / buildroot
```bash
# 1) 放包定义 + 幂等追加 auto/Config.in + 同步源码（rsync 排除 build*/、*.user、.git 等）
SDK_ROOT=/path/to/tina-sdk bash notes/hmi-t113s4/install-buildroot.sh

# 2) 打开包
bash ./build.sh menuconfig          # 或直接在 defconfig 里加 BR2_PACKAGE_HMI_T113S4=y

# 3) 编译
bash ./build.sh package hmi-t113s4

# 4) 打包 / 烧写后重启，S70launcher 会拉起 /usr/bin/QtApplication
```
`hmi-t113s4.mk` 走 `generic-package`：`SITE_METHOD = local` → `CONFIGURE_CMDS` 用 SDK 的 `$(QT5_QMAKE)`（若该 SDK 变量叫 `TARGET_QMAKE`，改 `.mk` 里 `HMI_T113S4_QMAKE` 一行即可）→ `BUILD_CMDS` → `INSTALL_TARGET_CMDS` 改名安装 + 装 `/etc/hmi.ini` + `lrelease` 出 `.qm`。

### Qt Creator 自定义 Deploy 步骤
```bash
bash deploy/adb-deploy.sh build/HMI-T113S4
# 等价于约定命令：
adb push build/HMI-T113S4 /tmp/ && adb shell "cp /tmp/HMI-T113S4 /usr/bin/QtApplication && sync && /etc/init.d/S70launcher stop; /etc/init.d/S70launcher start"
```

---

## 四、M1 验收清单

- [x] `python tools/verify.py` 通过：41 源文件 / 42 头 / 9 导航 / 10 页面 / 8 服务，moc 覆盖、include 闭合、qrc 与 .ts 合法、括号配对、硬约定 + 设计模式基础设施检查
- [ ] `file` / `readelf` 显示 **ARM EABI5 + libQt5Widgets.so.5**（需交叉 Kit）
- [ ] `bash ./build.sh package hmi-t113s4` 通过（需 SDK）
- [ ] 重启进 HMI（由 S70launcher 拉起 `/usr/bin/QtApplication`）
- [ ] 状态栏时间 / WiFi / ETH / ADB 实时刷新；任一命令失败时 stderr 出现在底部日志

> 本机仅有 Qt 4.8.7（无 Qt5 qmake / 交叉链），因此**未在本机做真实编译**，交付前已用 `tools/verify.py` 做结构级验证。
> 首次在 Qt Creator 交叉 Kit 上构建时，请把编译错误发我，我按日志一次性修好。

---

## 五、设计模式怎么落的（详见 docs/DESIGN-PATTERNS.md）

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

## 六、后续里程碑的开关（架构已预留，不返工）

| 里程碑 | 已就位 | 待接入 |
|---|---|---|
| M2 WiFi / ETH / ADB | 服务与页面命令已可跑（`wifi -o sta/-s/-c/-d`、`iw` 回退、`ifconfig`/`udhcpc`/`ping`、`S50adb_start`）；状态栏由 `StatusBus` 广播，`ServiceRegistry` 按各自间隔轮询（WiFi/ETH/ADB 5s） | 板端实测 |
| M3 I2C / RS485 / GPADC | `I2cService` 只读扫描实现、`Rs485Service` QSerialPort 收发、`GpadcService` 找 eventN + QSocketNotifier + 键码映射 | 指定地址读寄存器、全地址扫描移入工作线程、跳线帽实测 |
| M4 多媒体 | `MediaService` 播放/音量/错误日志、MediaPage 控件与「软解」标注 | 追加 `QT += multimediawidgets` 后把 `QVideoWidget` 绑到 `MediaService::player()` |
| M5 CAN | CanPage 设备发现/位速率/up/loopback/cansend 命令链；内核开关说明写在页面提示 | 改 `config-5.4` 开 `CONFIG_CAN/CAN_RAW/CAN_DEV/AWLINK_SUN8I`，再 `qmake CONFIG+=hmi_can`（自动 `QT += serialbus` + `HMI_HAVE_CAN`），补 QCanBus 收发 |
| M6 打磨 | 首页内存/负载/温度、页面 `onEnter/onLeave` 钩子、配置化字体与日志行数 | 文件管理 QFileSystemModel、watchdog、页面按需加载、启动 <3s |

---

## 七、教学点

1. **buildroot 包三件套**：`Config.in`（Kconfig 符号 `BR2_PACKAGE_HMI_T113S4`）、`hmi-t113s4.mk`（generic-package）、`auto/Config.in` 的 `source` 行。
2. **generic-package 生命周期**：`SITE` → `CONFIGURE_CMDS` → `BUILD_CMDS` → `INSTALL_TARGET_CMDS`。
3. **local SITE**：源码放在 `package/allwinner/auto/qt-demo/HMI-T113S4/`，`sync-src.sh` 用 rsync 排除 `build*/`、`*.user`、`.git`、moc/qrc 产物。
4. **qmake mkspec**：由 SDK Qt5 包提供 `$(QT5_QMAKE)`，不改 `.pro` 里的平台路径。
5. **Creator 部署链**：编译产物 → `adb push /tmp` → 改名 `/usr/bin/QtApplication` → 重启 S70launcher。
