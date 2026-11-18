#include "appsettings.h"
#include "singleton.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

// 板端是 7 寸屏，默认全屏；Linux 桌面调试默认窗口化，要全屏在 ini 里写 ui/fullScreen=true
bool defaultFullScreen()
{
#ifdef HMI_DEVICE_TARGET
    return true;
#else
    return false;
#endif
}

// 多媒体页扫描的目录：板端挂载点，桌面用系统媒体库
QStringList defaultMediaRoots()
{
#ifdef HMI_DEVICE_TARGET
    return QStringList() << QStringLiteral("/mnt/usb") << QStringLiteral("/usr/share")
                         << QStringLiteral("/mnt") << QStringLiteral("/media");
#else
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
#endif
}

} // namespace

AppSettings *AppSettings::instance()
{
    return hmi::singleton<AppSettings>([]() { return new AppSettings; });
}

QString AppSettings::platformConfigPath()
{
#ifdef HMI_DEVICE_TARGET
    return QStringLiteral("/etc/hmi.ini");
#else
    // 桌面端不写 /etc（普通用户无权限）：优先程序目录，其次当前目录的 etc/
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/hmi.ini");
    if (QFileInfo::exists(beside))
        return beside;
    const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/etc/hmi.ini");
    if (QFileInfo::exists(local))
        return local;
    return beside;
#endif
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_fontPixelSize(17)              // 与 UiMetrics::TextPixelSize 对齐；px 锚定，不受屏幕 DPI 影响
    , m_logMaxLines(500)
    , m_processTimeoutMs(4000)
    , m_fullScreen(defaultFullScreen())
    , m_simulated(false)
    , m_wifiLogLevel(QStringLiteral("error"))
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
    // ui/fontSize 历史上按 pt 填，现在按 px 解释；范围放宽到 12–32px
    if (settings.contains(QStringLiteral("ui/fontSize")))
        m_fontPixelSize    = qBound(12, settings.value(QStringLiteral("ui/fontSize")).toInt(), 32);
    if (settings.contains(QStringLiteral("ui/logMaxLines")))
        m_logMaxLines      = qBound(100, settings.value(QStringLiteral("ui/logMaxLines")).toInt(), 5000);
    if (settings.contains(QStringLiteral("process/timeoutMs")))
        m_processTimeoutMs = qBound(3000, settings.value(QStringLiteral("process/timeoutMs")).toInt(), 5000);
    if (settings.contains(QStringLiteral("ui/fullScreen")))
        m_fullScreen       = settings.value(QStringLiteral("ui/fullScreen")).toBool();
    if (settings.contains(QStringLiteral("ui/simulate")))
        m_simulated        = settings.value(QStringLiteral("ui/simulate")).toBool();

    // WiFi 驱动日志等级：只接受白名单里的值，写错了退回 error。
    // 不让它把任意字符串透传给 wifi -D —— 那是把配置错误变成运行期怪行为。
    if (settings.contains(QStringLiteral("wifi/logLevel"))) {
        const QString lv = settings.value(QStringLiteral("wifi/logLevel")).toString().trimmed();
        m_wifiLogLevel = wifiLogLevels().contains(lv, Qt::CaseInsensitive)
                         ? lv.toLower() : QStringLiteral("error");
    }

    m_mediaRoots = defaultMediaRoots();
    const QString roots = settings.value(QStringLiteral("media/roots")).toString();
    if (!roots.trimmed().isEmpty())
        m_mediaRoots = roots.split(QLatin1Char(','), Qt::SkipEmptyParts);

    emit reloaded();
}

QString AppSettings::path() const
{
    return m_path;
}

int AppSettings::fontPixelSize() const
{
    return m_fontPixelSize;
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

QString AppSettings::wifiLogLevel() const
{
    return m_wifiLogLevel;
}

// 与 `wifi -D` 的参数完全一致（见 wifi -h 的 other Options 段）。
// open/close 控制的是「是否打印 path/file/line 前缀」，不是等级，
// 但既然驱动认这些参数就一并放行，免得用户想开路径打印时被拒。
QStringList AppSettings::wifiLogLevels()
{
    return QStringList() << QStringLiteral("error") << QStringLiteral("warn")
                         << QStringLiteral("info") << QStringLiteral("debug")
                         << QStringLiteral("dump") << QStringLiteral("open")
                         << QStringLiteral("close");
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
