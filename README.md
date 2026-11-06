# GloveBraidingCtl 软件框架空壳

由 `glove-braiding-ctl`（Qt 4.8.7）裁剪移植而来，**只保留软件框架，不含任何业务功能**，目标工具链为 **Qt 5.15.2**。

![首页](docs/screenshot-home.png)
![设置页](docs/screenshot-setting.png)

## 一、保留了什么

| 部分 | 文件 | 说明 |
|---|---|---|
| 程序入口 | `main.cpp` | 应用初始化、日志、配置、样式表加载、启动自检 |
| 主窗口 | `mainwindow.h/.cpp` | 标题栏 + 页面容器 + 状态栏三段式布局，页面切换与生命周期钩子 |
| 页面基类 | `views/baseview.h` | 继承后实现 `title()`，即可被主窗口装载 |
| 示例页面 | `views/homeview.*`、`views/settingview.*` | 首页与设置页，演示布局与控件样式 |
| 日志 | `framework/logger.*` | `qDebug/qInfo/qWarning` 统一落盘，按大小滚动 |
| 配置 | `framework/appconfig.*` | `QSettings` 封装，默认读写 `config/app.ini` |
| 样式 | `resources/qss/style.qss` | 沿用原工程的配色与控件样式 |
| 资源 | `resources/resources.qrc` | qss 与滚动条箭头图标 |

## 二、删掉了什么

原工程 74 个模块、316 个源文件全部移除，包括：

- 编织业务：花型、密度、橡筋、色纱、指尖、行程调整、圈数设定等
- 设备控制：电机参数、伺服、检套、传感器、电磁铁、扩展板测试
- 通信：Modbus 协议线程、串口封装、ISP 烧录、USB 检测
- 数据：SQLite 数据库、报警记录、导入导出、注册与产品密钥
- 其它：多语言、二维码、升级、主题管理、软键盘等

## 三、编译

```bash
# 桌面（Qt 5.15.2）
qmake GloveBraidingCtl.pro
make -j$(nproc)
./GloveBraidingCtl

# 无显示环境自检（CI 用）
QT_QPA_PLATFORM=offscreen ./GloveBraidingCtl --selftest
```

交叉编译时把 `qmake` 换成你的 T113 交叉版 qmake 即可，`.pro` 不含任何平台相关路径。

## 四、怎么加一个新页面

1. 在 `views/` 下新建 `xxxview.h/.cpp`，继承 `BaseView`
2. 实现 `QString title() const override`
3. 把文件加入 `views/views.pri`
4. 在 `MainWindow` 构造函数里 `addView(new XxxView(this))`

页面切换：

```cpp
window->setCurrentIndex(1);          // 直接切
emit requestPage(1);                 // 页面内发起，由 MainWindow 接管
```

生命周期钩子 `onEnter()` / `onLeave()` 可用于刷新数据、暂停定时器。

## 五、从 Qt 4.8.7 到 Qt 5.15.2 的改动

原工程里实际命中的 Qt4 专属写法只有 5 处，处理如下：

| 原写法 | 处理 |
|---|---|
| `QTextCodec::setCodecForTr()` | 删除，Qt5 默认 UTF-8，源码保持 UTF-8 编码 |
| `QTextCodec::setCodecForCStrings()` | 删除 |
| `Q_WS_QWS`（13 处） | 改用 `Q_OS_LINUX` 或直接删除，Qt5 无 QWS |
| `QString::toAscii()`（4 处） | 改为 `toLatin1()` |
| `QDesktopWidget`（2 处） | 改为 `QGuiApplication::primaryScreen()` 取屏幕尺寸 |

工程文件层面：

- `QT += core gui` 之外补 `widgets`（Qt5 把 QWidget 从 QtGui 拆出）
- 保留 `greaterThan(QT_MAJOR_VERSION, 4): QT += widgets`，兼容写法
- 移除 `-std=c++11` 手工指定，改用 `CONFIG += c++11`
- `.pro` 里不再做字符串宏转义，版本号统一放 `framework/appversion.h`

## 六、注意事项

- 源码统一 UTF-8（无 BOM）。原工程的 `style.qss` 带 BOM，已去除，否则首条规则会被忽略。
- 原 qss 里 `url(:/decrease.png)`、`url(:/increase.png)` 指向的图片在仓库中不存在，已改为 `none`。
- 交叉编译到 T113 时，如果只用 linuxfb，`QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0`；字体目录记得设 `QT_QPA_FONTDIR`。

## 七、运行参数

| 参数 | 说明 |
|---|---|
| 无参数 | 正常启动 |
| `--selftest` | 启动 800ms 后自动退出，用于 CI / 无显示环境验证 |
| `--screenshot <file>` | 启动 500ms 后把主窗口截图保存为 PNG |
| `--page <index>` | 启动后直接切到指定页面（0 首页，1 设置页） |

无显示环境下验证渲染：

```bash
QT_QPA_PLATFORM=offscreen ./GloveBraidingCtl --page 1 --selftest --screenshot /tmp/shot.png
```

## 八、目录结构

```
GloveBraidingCtl/
├── GloveBraidingCtl.pro
├── main.cpp
├── mainwindow.h / mainwindow.cpp
├── framework/
│   ├── framework.pri
│   ├── appversion.h
│   ├── logger.h / logger.cpp
│   └── appconfig.h / appconfig.cpp
├── views/
│   ├── views.pri
│   ├── baseview.h / baseview.cpp
│   ├── homeview.h / homeview.cpp
│   └── settingview.h / settingview.cpp
├── resources/
│   ├── resources.qrc
│   ├── qss/style.qss
│   └── gloveIcon/common/*.png
└── docs/
```
