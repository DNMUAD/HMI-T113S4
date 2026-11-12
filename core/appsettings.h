#ifndef HMI_CORE_APPSETTINGS_H
#define HMI_CORE_APPSETTINGS_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

// 设计模式 #10：单例（Singleton）+ 外观（Facade）
//
// /etc/hmi.ini 的读取原本散在 main.cpp 里，键名、默认值、钳制范围到处都是。
// AppSettings 把「读 ini + 类型转换 + 范围钳制 + 默认值」收进一个门面，
// 其它模块只问语义化接口：fontPixelSize() / processTimeoutMs() / mediaRoots()。
class AppSettings : public QObject
{
    Q_OBJECT
public:
    static AppSettings *instance();

    // 板端读 /etc/hmi.ini；Windows 桌面版读程序目录下的 hmi.ini。
    // 文件不存在时 QSettings 读不到任何键，调用方保留平台默认值。
    static QString platformConfigPath();

    void load(const QString &path = QString());
    QString path() const;

    int  fontPixelSize() const;         // 12–32（px；pt 会随屏幕 DPI 放大，板端不可控）
    int  logMaxLines() const;           // 100–5000
    int  processTimeoutMs() const;      // 3000–5000（硬约定）
    bool fullScreen() const;
    bool simulated() const;             // 桌面模拟执行器
    void setSimulated(bool enabled);
    QStringList mediaRoots() const;

    QString value(const QString &key, const QString &fallback = QString()) const;
    QStringList keys() const;

signals:
    void reloaded();

private:
    explicit AppSettings(QObject *parent = nullptr);

    QString                 m_path;
    QHash<QString, QString> m_values;
    int                     m_fontPixelSize;
    int                     m_logMaxLines;
    int                     m_processTimeoutMs;
    bool                    m_fullScreen;
    bool                    m_simulated;
    QStringList             m_mediaRoots;
};

#endif // HMI_CORE_APPSETTINGS_H
