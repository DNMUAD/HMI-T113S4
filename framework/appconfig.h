#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QObject>
#include <QString>
#include <QVariant>

class QSettings;

// 配置封装：默认读写 <程序目录>/config/app.ini
class AppConfig : public QObject
{
    Q_OBJECT
public:
    static AppConfig *instance();

    void load(const QString &filePath = QString());

    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);
    void sync();

    QString filePath() const;

private:
    explicit AppConfig(QObject *parent = nullptr);
    ~AppConfig() override;

    QSettings *m_settings;
    QString    m_path;
};

#endif // APPCONFIG_H
