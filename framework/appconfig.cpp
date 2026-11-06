#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>

AppConfig *AppConfig::instance()
{
    static AppConfig inst;
    return &inst;
}

AppConfig::AppConfig(QObject *parent)
    : QObject(parent)
    , m_settings(nullptr)
{
}

AppConfig::~AppConfig()
{
    sync();
    delete m_settings;
}

void AppConfig::load(const QString &filePath)
{
    QString path = filePath;
    if (path.isEmpty()) {
        QDir dir(QCoreApplication::applicationDirPath());
        dir.mkpath(QStringLiteral("config"));
        path = dir.filePath(QStringLiteral("config/app.ini"));
    }

    delete m_settings;
    m_path = path;
    m_settings = new QSettings(path, QSettings::IniFormat);
    m_settings->setIniCodec("UTF-8");

    // 首次运行写入默认配置，方便直接看到文件
    if (!QFile::exists(path)) {
        m_settings->setValue(QStringLiteral("app/language"), QStringLiteral("zh_CN"));
        m_settings->setValue(QStringLiteral("app/logLevel"), QStringLiteral("info"));
        m_settings->sync();
    }
}

QVariant AppConfig::value(const QString &key, const QVariant &defaultValue) const
{
    if (!m_settings)
        return defaultValue;
    return m_settings->value(key, defaultValue);
}

void AppConfig::setValue(const QString &key, const QVariant &value)
{
    if (!m_settings)
        return;
    m_settings->setValue(key, value);
}

void AppConfig::sync()
{
    if (m_settings)
        m_settings->sync();
}

QString AppConfig::filePath() const
{
    return m_path;
}
