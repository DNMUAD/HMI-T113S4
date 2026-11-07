#include "appsettings.h"
#include "singleton.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>

namespace {

// 板端是 7 寸屏，默认全屏；Windows 桌面调试默认窗口化，要全屏在 ini 里写 ui/fullScreen=true
bool defaultFullScreen()
{
#ifdef Q_OS_WIN
    return false;
#else
    return true;
#endif
}

// 多媒体页扫描的目录：板端挂载点，桌面用系统媒体库
QStringList defaultMediaRoots()
{
#ifdef Q_OS_WIN
    QStringList roots;
    const QStringList candidates = QStringList()
        << QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
        << QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
        << QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    for (int i = 0; i < candidates.size(); ++i) {
        if (!candidates.at(i).isEmpty() && !roots.contains(candidates.at(i)))
            roots << candidates.at(i);
    }
    return roots;
#else
    return QStringList() << QStringLiteral("/mnt/usb") << QStringLiteral("/usr/share")
                         << QStringLiteral("/mnt") << QStringLiteral("/media");
#endif
}

} // namespace

AppSettings *AppSettings::instance()
{
    return hmi::singleton<AppSettings>([]() { return new AppSettings; });
}

QString AppSettings::platformConfigPath()
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QStringLiteral("/hmi.ini");
#else
    return QStringLiteral("/etc/hmi.ini");
#endif
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_fontPointSize(14)              // 与 UiMetrics::TextPointSize 对齐；1024x600 下 16pt 偏大
    , m_logMaxLines(500)
    , m_processTimeoutMs(4000)
    , m_fullScreen(defaultFullScreen())
    , m_simulated(false)
    , m_mediaRoots(defaultMediaRoots())
{
}

void AppSettings::load(const QString &path)
{
    m_path = path.isEmpty() ? platformConfigPath() : path;

    QSettings settings(m_path, QSettings::IniFormat);

    m_values.clear();
    const QStringList allKeys = settings.allKeys();
    for (int i = 0; i < allKeys.size(); ++i)
        m_values.insert(allKeys.at(i), settings.value(allKeys.at(i)).toString());

    // 只有 ini 里确实写了才覆盖，避免「文件不存在」时把平台默认值冲掉
    if (settings.contains(QStringLiteral("ui/fontSize")))
        m_fontPointSize    = qBound(12, settings.value(QStringLiteral("ui/fontSize")).toInt(), 24);
    if (settings.contains(QStringLiteral("ui/logMaxLines")))
        m_logMaxLines      = qBound(100, settings.value(QStringLiteral("ui/logMaxLines")).toInt(), 5000);
    if (settings.contains(QStringLiteral("process/timeoutMs")))
        m_processTimeoutMs = qBound(3000, settings.value(QStringLiteral("process/timeoutMs")).toInt(), 5000);
    if (settings.contains(QStringLiteral("ui/fullScreen")))
        m_fullScreen       = settings.value(QStringLiteral("ui/fullScreen")).toBool();
    if (settings.contains(QStringLiteral("ui/simulate")))
        m_simulated        = settings.value(QStringLiteral("ui/simulate")).toBool();

    m_mediaRoots = defaultMediaRoots();
    const QString roots = settings.value(QStringLiteral("media/roots")).toString();
    if (!roots.trimmed().isEmpty())
        m_mediaRoots = roots.split(QLatin1Char(','), QString::SkipEmptyParts);

    emit reloaded();
}

QString AppSettings::path() const
{
    return m_path;
}

int AppSettings::fontPointSize() const
{
    return m_fontPointSize;
}

int AppSettings::logMaxLines() const
{
    return m_logMaxLines;
}

int AppSettings::processTimeoutMs() const
{
    return m_processTimeoutMs;
}

bool AppSettings::fullScreen() const
{
    return m_fullScreen;
}

bool AppSettings::simulated() const
{
    return m_simulated;
}

void AppSettings::setSimulated(bool enabled)
{
    m_simulated = enabled;
}

QStringList AppSettings::mediaRoots() const
{
    return m_mediaRoots;
}

QString AppSettings::value(const QString &key, const QString &fallback) const
{
    return m_values.value(key, fallback);
}

QStringList AppSettings::keys() const
{
    return m_values.keys();
}
