#include "appsettings.h"
#include "commandinvoker.h"
#include "gpadcservice.h"
#include "logpanel.h"
#include "loggingcommandrunner.h"
#include "mainwindow.h"
#include "navigationbus.h"
#include "processrunner.h"
#include "simulatedcommandrunner.h"
#include "upgradeservice.h"
#include "webshareservice.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QTranslator>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace {

const char *kInstanceServer = "HMI-T113S4.single";

// 已有实例时通过本地 socket 唤醒它，本进程直接退出。
bool tryActivateExistingInstance()
{
    QLocalSocket sock;
    sock.connectToServer(QString::fromLatin1(kInstanceServer));
    if (!sock.waitForConnected(200))
        return false;
    sock.write("activate");
    sock.flush();
    sock.waitForBytesWritten(200);
    sock.disconnectFromServer();
    return true;
}

void installTranslators(QApplication &app)
{
    const QString base = QStringLiteral("HMI-T113S4_");
    const QStringList dirs = QStringList()
        << QStringLiteral(":/i18n")
        << QStringLiteral("/usr/share/hmi/i18n")
        << QCoreApplication::applicationDirPath() + QStringLiteral("/i18n");

    QStringList locales = QLocale::system().uiLanguages();
    locales << QLocale::system().name() << QStringLiteral("zh_CN");
    locales.removeDuplicates();

    for (const QString &dir : dirs) {
        for (const QString &loc : locales) {
            QTranslator *t = new QTranslator(&app);
            if (t->load(base + QLocale(loc).name(), dir) || t->load(base + loc, dir)) {
                app.installTranslator(t);
                return;
            }
            delete t;
        }
    }
    // 中文由 rootfs 的 wqy-zenhei + fontconfig 提供；无 .qm 时源码内 tr() 中文照常可用。
}

void applyStyleSheet(QApplication &app)
{
    QFile f(QStringLiteral(":/qss/style.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("style.qss not found in resources");
        return;
    }
    app.setStyleSheet(QString::fromUtf8(f.readAll()));
}

// 等升级服务进入某个状态，或超时。返回是否在超时前命中。
bool waitUpgradeState(UpgradeService *svc,
                      const std::function<bool (UpgradeService::State)> &pred,
                      int timeoutMs)
{
    if (pred(svc->state()))
        return true;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool hit = false;
    QObject::connect(svc, &UpgradeService::stateChanged, &loop,
                     [&](UpgradeService::State s) {
        if (pred(s)) {
            hit = true;
            loop.quit();
        }
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    return hit;
}

// --upgrade-selftest：无人值守跑 扫描 → 校验 → 安装 → 回滚，用于验收与现场排查。
// 安装后立刻回滚，不改变设备最终状态；退出码 0 表示全链路通过。
int runUpgradeSelfTest()
{
    UpgradeService *svc = UpgradeService::instance();

    svc->scan();
    qInfo("[selftest] scan    state=%d media=%s pkg=%d",
          int(svc->state()), qPrintable(svc->mediaPath()), int(svc->hasPackage()));
    if (!svc->hasPackage()) {
        qInfo("[selftest] 没有可用升级包：%s", qPrintable(svc->message()));
        return 3;
    }

    svc->verify();
    waitUpgradeState(svc, [](UpgradeService::State s) {
        return s == UpgradeService::Verified || s == UpgradeService::VerifyFailed;
    }, 20000);
    const bool verified = (svc->state() == UpgradeService::Verified);
    qInfo("[selftest] verify  %s  %s", verified ? "OK  " : "FAIL", qPrintable(svc->message()));
    if (!verified)
        return 4;

    svc->install();
    waitUpgradeState(svc, [](UpgradeService::State s) {
        return s == UpgradeService::Installed || s == UpgradeService::InstallFailed;
    }, 30000);
    const bool installed = (svc->state() == UpgradeService::Installed);
    qInfo("[selftest] install %s  %s", installed ? "OK  " : "FAIL", qPrintable(svc->message()));
    if (!installed)
        return 5;

    svc->rollback();
    qInfo("[selftest] rollback %s", qPrintable(svc->message()));
    return 0;
}

// --gpadc-selftest：无人值守跑通「按键 → 动作 → 切页」整条链路。
//
// 为什么要它：GPADC 的真机验证需要板子连着按键，桌面 Kit 没有 /dev/input/eventN，
// 每次改派发逻辑都只能上板点按钮。这里用 simulateKey() 注入与硬件完全相同的事件，
// 走同一段 handleCode()/dispatch() 代码，因此可以在 PC 上验证：
//   · 动作表里确实有切页动作；
//   · 绑定写入/读回正确；
//   · 按键按下确实派发到了对应动作，动作确实发出了切页请求。
// 退出码 0 = 全部通过。
int runGpadcSelfTest(MainWindow *window)
{
    GpadcService *svc = GpadcService::instance();
    NavigationBus *nav = NavigationBus::instance();

    int failures = 0;
    const auto expect = [&failures](bool ok, const char *what) {
        qInfo("[gpadc] %s  %s", ok ? "OK  " : "FAIL", what);
        if (!ok)
            ++failures;
    };

    // 自检会改写绑定并落盘。先把原值存下来，结束时无条件恢复——
    // 自检是「取证」工具，不该改变设备状态（与 --upgrade-selftest 装完即回滚同一原则）。
    const QList<GpadcService::KeyInfo> keys = GpadcService::keys();
    QHash<int, QString> original;
    for (const GpadcService::KeyInfo &k : keys)
        original.insert(k.code, svc->binding(k.code));

    // 1) 动作表：内置两项 + 切页示例五项
    const QList<GpadcService::Action> actions = svc->actions();
    QStringList ids;
    for (const GpadcService::Action &a : actions)
        ids << a.id;
    qInfo("[gpadc] 动作表：%s", qPrintable(ids.join(QStringLiteral(", "))));
    expect(ids.contains(QStringLiteral("none")), "内置动作 none 存在");
    expect(ids.contains(QStringLiteral("echo")), "内置动作 echo 存在");
    expect(ids.contains(QStringLiteral("gpadc.nav-prev")), "示例动作 nav-prev 存在");
    expect(ids.contains(QStringLiteral("gpadc.nav-next")), "示例动作 nav-next 存在");
    expect(ids.contains(QStringLiteral("gpadc.nav-home")), "示例动作 nav-home 存在");
    expect(ids.contains(QStringLiteral("gpadc.goto-wifi")), "示例动作 goto-wifi 存在");
    expect(ids.contains(QStringLiteral("gpadc.goto-bt")), "示例动作 goto-bt 存在");

    // 2) 绑定写读：改成 nav-next 再读回
    svc->setBinding(GpadcService::KeyK2, QStringLiteral("gpadc.nav-next"));
    expect(svc->binding(GpadcService::KeyK2) == QStringLiteral("gpadc.nav-next"),
           "K2 绑定写读一致");

    // 3) 派发：按一下 K2，应当发出「下一页」，当前页随之变化
    const QString before = nav->currentPageId();
    svc->simulateKey(GpadcService::KeyK2, 1);
    svc->simulateKey(GpadcService::KeyK2, 0);
    const QString after = nav->currentPageId();
    qInfo("[gpadc] 按 K2：页 %s → %s", qPrintable(before), qPrintable(after));
    expect(before != after, "K2 按一下确实切到了另一页");
    expect(svc->pressCount(GpadcService::KeyK2) >= 1, "K2 按下计数已累加");

    // 4) 绝对跳转：goto-wifi 应落到 wifi 页（按 id 而不是下标）
    svc->setBinding(GpadcService::KeyK3, QStringLiteral("gpadc.goto-wifi"));
    svc->simulateKey(GpadcService::KeyK3, 1);
    expect(nav->currentPageId() == QStringLiteral("wifi"),
           "K3 绑 goto-wifi 后按下落到 wifi 页");
    svc->setBinding(GpadcService::KeyK3, QStringLiteral("gpadc.nav-home"));
    svc->simulateKey(GpadcService::KeyK3, 1);
    expect(nav->currentPageId() == QStringLiteral("home"), "K3 绑 nav-home 后按下回到总览");

    // 5) 无动作：绑 none 时不应切页
    svc->setBinding(GpadcService::KeyK1, QStringLiteral("none"));
    const QString beforeNone = nav->currentPageId();
    svc->simulateKey(GpadcService::KeyK1, 1);
    expect(nav->currentPageId() == beforeNone, "绑 none 时按下不切页");

    // 6) 恢复原有绑定（不覆盖用户的配置）
    for (const GpadcService::KeyInfo &k : keys) {
        const QString saved = original.value(k.code);
        if (!saved.isEmpty())
            svc->setBinding(k.code, saved);
    }
    qInfo("[gpadc] 绑定已恢复：%s", qPrintable(svc->bindingSummary()));

    Q_UNUSED(window)
    qInfo("[gpadc] 自检结束，失败 %d 项", failures);
    return failures == 0 ? 0 : 6;
}

void hmiMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const QByteArray line = msg.toLocal8Bit();
    std::fprintf(stderr, "%s\n", line.constData());
    std::fflush(stderr);

    if (!QCoreApplication::instance())
        return;

    LogPanel::Level level = LogPanel::Info;
    switch (type) {
    case QtDebugMsg:    level = LogPanel::Debug; break;
    case QtWarningMsg:  level = LogPanel::Warn;  break;
    case QtCriticalMsg:
    case QtFatalMsg:    level = LogPanel::Error; break;
    default:            level = LogPanel::Info;  break;
    }
    LogPanel::post(QStringLiteral("[Qt] ") + msg, level);
}

} // namespace

int main(int argc, char *argv[])
{
    // 板端是固定分辨率的嵌入式屏（800x480），界面按物理像素 1:1 排版。
    // 不能开 AA_EnableHighDpiScaling：eglfs 上报的 DPI 约 130-190，
    // 开启后 Qt 按 round(dpi/96) 取整放大整窗（常见 ×2），800x480 的屏
    // 会被排成 1600x960 的逻辑尺寸，控件全被撑出屏幕——实机「挤在一起、字体很大」即此。
    // 字号统一改用像素（见 UiMetrics），不再受 DPI 影响。
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("HMI-T113S4"));
    app.setApplicationVersion(QStringLiteral("1.0.0-M1"));
    app.setOrganizationName(QStringLiteral("DNMUAD"));

    qInstallMessageHandler(hmiMessageHandler);

    // 单实例：root + init.d 拉起，避免重复占用 /dev 与 QLocalServer
    if (tryActivateExistingInstance())
        return 0;

    QLocalServer server;
    QLocalServer::removeServer(QString::fromLatin1(kInstanceServer));
    if (!server.listen(QString::fromLatin1(kInstanceServer)))
        qWarning("single-instance server: %s", qPrintable(server.errorString()));

    // 全局配置：板端 /etc/hmi.ini，Windows 桌面版读程序目录下的 hmi.ini（不存在则全走平台默认值）
    // 单例 + 外观：键名、默认值、钳制范围都收在 AppSettings 里
    AppSettings *settings = AppSettings::instance();
    settings->load(AppSettings::platformConfigPath());

    const QStringList args = app.arguments();
    const bool windowed = args.contains(QStringLiteral("--windowed"));
    const bool simulate = args.contains(QStringLiteral("--simulate")) || settings->simulated();

    // 全局字体只设字号，字体族交给 rootfs 的 fontconfig（wqy-zenhei）。
    // 用像素而非 pt：pt 会随屏幕物理 DPI 换算，板端 7 寸屏上会放大到撑破控件。
    QFont font = app.font();
    font.setPixelSize(settings->fontPixelSize());
    app.setFont(font);

    installTranslators(app);
    applyStyleSheet(app);

    LogPanel::instance()->setMaxLines(settings->logMaxLines());
    ProcessRunner::instance()->setDefaultTimeout(settings->processTimeoutMs());

    // 策略注入：桌面调试可用预置输出顶替真实硬件，业务代码零改动
    if (simulate) {
        ProcessRunner::instance()->installBackend(
            new LoggingCommandRunner(new SimulatedCommandRunner));
    }
    // 命令模式：Invoker 与执行器解耦，这里完成最后一次装配
    CommandInvoker::instance()->setRunner(ProcessRunner::instance()->backend());

    LogPanel::post(QStringLiteral("HMI-T113S4 %1 启动，配置 %2，全局字体 %3px，命令超时 %4ms，执行器 %5")
                   .arg(app.applicationVersion(), settings->path())
                   .arg(settings->fontPixelSize())
                   .arg(settings->processTimeoutMs())
                   .arg(ProcessRunner::instance()->backendName()),
                   LogPanel::Ok);

    MainWindow w;

    // 局域网网页文件管理：默认随程序启动，ini 里 web/autostart=false 可关掉
    if (settings->value(QStringLiteral("web/autostart"), QStringLiteral("true"))
            .compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0) {
        WebShareService::instance()->start();
    }
    QObject::connect(&server, &QLocalServer::newConnection, [&server, &w]() {
        QLocalSocket *conn = server.nextPendingConnection();
        if (conn) {
            QObject::connect(conn, &QLocalSocket::disconnected, conn, &QObject::deleteLater);
            conn->disconnectFromServer();
        }
        w.raise();
        w.activateWindow();
    });

    // 板端屏 800x480 的回归手段：--size 800x480 把窗口定到该尺寸，
    // 配合 --shot 就能在桌面上核对小屏排版，不必真的上板。
    const int sizeArg = args.indexOf(QStringLiteral("--size"));
    if (sizeArg > 0 && sizeArg + 1 < args.size()) {
        const QStringList wh = args.at(sizeArg + 1).split(QLatin1Char('x'));
        if (wh.size() == 2 && wh.at(0).toInt() > 0 && wh.at(1).toInt() > 0)
            w.resize(wh.at(0).toInt(), wh.at(1).toInt());
    }

    if (settings->fullScreen() && !windowed)
        w.showFullScreen();
    else
        w.show();

    // 升级链路自检：不依赖界面，跑完 扫描→校验→安装→回滚 即退出
    if (args.contains(QStringLiteral("--upgrade-selftest")))
        return runUpgradeSelfTest();

    // GPADC 链路自检：按键 → 动作 → 切页。主窗口必须已建好（页面注册了动作）
    if (args.contains(QStringLiteral("--gpadc-selftest")))
        return runGpadcSelfTest(&w);

    const int pageArg = args.indexOf(QStringLiteral("--page"));
    if (pageArg > 0 && pageArg + 1 < args.size())
        w.setCurrentPage(args.at(pageArg + 1).toInt());

    // UI 回归用：--page N --shot out.png 抓取指定页的控件渲染结果后退出。
    // QWidget::grab() 直接渲染控件树，不受窗口遮挡/最小化影响，可脚本化比对。
    const int shotArg = args.indexOf(QStringLiteral("--shot"));
    if (shotArg > 0 && shotArg + 1 < args.size()) {
        const QString shotPath = args.at(shotArg + 1);
        QTimer::singleShot(700, &w, [&w, shotPath]() {
            if (!w.grab().save(shotPath))
                qWarning("截图保存失败: %s", qPrintable(shotPath));
            QCoreApplication::quit();
        });
    }

    // 离屏自检：QT_QPA_PLATFORM=offscreen ./HMI-T113S4 --selftest
    if (args.contains(QStringLiteral("--selftest")))
        QTimer::singleShot(800, &app, &QCoreApplication::quit);

    const int ret = app.exec();
    ProcessRunner::instance()->cancelAll();
    return ret;
}
