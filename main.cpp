#include "mainwindow.h"
#include "appconfig.h"
#include "appversion.h"
#include "logger.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QDir>
#include <QFile>
#include <QDebug>

// 加载 qss 样式表
static void applyStyleSheet(QApplication &app)
{
    QFile file(QStringLiteral(":/qss/style.qss"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "style.qss not found in resources";
        return;
    }
    app.setStyleSheet(QString::fromUtf8(file.readAll()));
    file.close();
}

int main(int argc, char *argv[])
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral(APP_NAME));
    a.setApplicationVersion(QStringLiteral(APP_VERSION));
    a.setOrganizationName(QStringLiteral(APP_ORG));

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "HMI-T113S4_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    const QString appDir = QCoreApplication::applicationDirPath();

    Logger::instance()->init(QDir(appDir).filePath(QStringLiteral("logs")));
    qInfo() << "========================================";
    qInfo() << APP_NAME << APP_VERSION << "start";
    qInfo() << "application dir:" << appDir;

    AppConfig::instance()->load();
    qInfo() << "config file:" << AppConfig::instance()->filePath();

    applyStyleSheet(a);

    MainWindow w;
    w.show();

    // --page <index>：启动后直接切到指定页面
//   const QStringList args = a.arguments();
//   const int pageIndex = args.indexOf(QStringLiteral("--page"));
//   if (pageIndex > 0 && pageIndex + 1 < args.size())
//       w.setCurrentIndex(args.at(pageIndex + 1).toInt());

    const int ret = a.exec();
    qInfo() << "exit code:" << ret;

    Logger::instance()->shutdown();
    return ret;
}
