#include "appsettings.h"
#include "commandinvoker.h"
#include "logpanel.h"
#include "loggingcommandrunner.h"
#include "mainwindow.h"
#include "processrunner.h"
#include "simulatedcommandrunner.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QTranslator>
#include <QTimer>

#include <cstdio>

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
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
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

    // 全局配置 /etc/hmi.ini（只读；板端可改，桌面开发时不存在则走默认值）
    // 单例 + 外观：键名、默认值、钳制范围都收在 AppSettings 里
    AppSettings *settings = AppSettings::instance();
    settings->load(QStringLiteral("/etc/hmi.ini"));

    const QStringList args = app.arguments();
    const bool windowed = args.contains(QStringLiteral("--windowed"));
    const bool simulate = args.contains(QStringLiteral("--simulate")) || settings->simulated();

    // 全局字体只设字号，字体族交给 rootfs 的 fontconfig（wqy-zenhei）
    QFont font = app.font();
    font.setPointSize(settings->fontPointSize());
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

    LogPanel::post(QStringLiteral("HMI-T113S4 %1 启动，配置 %2，全局字体 %3pt，命令超时 %4ms，执行器 %5")
                   .arg(app.applicationVersion(), settings->path())
                   .arg(settings->fontPointSize())
                   .arg(settings->processTimeoutMs())
                   .arg(ProcessRunner::instance()->backendName()),
                   LogPanel::Ok);

    MainWindow w;
    QObject::connect(&server, &QLocalServer::newConnection, [&server, &w]() {
        QLocalSocket *conn = server.nextPendingConnection();
        if (conn) {
            QObject::connect(conn, &QLocalSocket::disconnected, conn, &QObject::deleteLater);
            conn->disconnectFromServer();
        }
        w.raise();
        w.activateWindow();
    });

    if (settings->fullScreen() && !windowed)
        w.showFullScreen();
    else
        w.show();

    const int pageArg = args.indexOf(QStringLiteral("--page"));
    if (pageArg > 0 && pageArg + 1 < args.size())
        w.setCurrentPage(args.at(pageArg + 1).toInt());

    // 离屏自检：QT_QPA_PLATFORM=offscreen ./HMI-T113S4 --selftest
    if (args.contains(QStringLiteral("--selftest")))
        QTimer::singleShot(800, &app, &QCoreApplication::quit);

    const int ret = app.exec();
    ProcessRunner::instance()->cancelAll();
    return ret;
}
