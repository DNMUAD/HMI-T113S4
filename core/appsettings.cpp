#include "appsettings.h"
#include "singleton.h"

#include <QSettings>

AppSettings *AppSettings::instance()
{
    return hmi::singleton<AppSettings>([]() { return new AppSettings; });
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_fontPointSize(16)
    , m_logMaxLines(500)
    , m_processTimeoutMs(4000)
    , m_fullScreen(true)
    , m_simulated(false)
{
}

void AppSettings::load(const QString &path)
{
    m_path = path.isEmpty() ? QStringLiteral("/etc/hmi.ini") : path;

    QSettings settings(m_path, QSettings::IniFormat);

    m_values.clear();
    const QStringList allKeys = settings.allKeys();
    for (int i = 0; i < allKeys.size(); ++i)
        m_values.insert(allKeys.at(i), settings.value(allKeys.at(i)).toString());

    m_fontPointSize    = qBound(12, settings.value(QStringLiteral("ui/fontSize"), 16).toInt(), 24);
    m_logMaxLines      = qBound(100, settings.value(QStringLiteral("ui/logMaxLines"), 500).toInt(), 5000);
    m_processTimeoutMs = qBound(3000, settings.value(QStringLiteral("process/timeoutMs"), 4000).toInt(), 5000);
    m_fullScreen       = settings.value(QStringLiteral("ui/fullScreen"), true).toBool();
    m_simulated        = settings.value(QStringLiteral("ui/simulate"), false).toBool();

    const QString roots = settings.value(QStringLiteral("media/roots")).toString();
    if (roots.trimmed().isEmpty())
        m_mediaRoots = QStringList() << QStringLiteral("/mnt/usb") << QStringLiteral("/usr/share")
                                     << QStringLiteral("/mnt") << QStringLiteral("/media");
    else
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
