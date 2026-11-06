#ifndef LOGGER_H
#define LOGGER_H

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>

// 轻量日志封装：
//   qDebug/qInfo/qWarning/qCritical 统一输出到控制台和 logs/app.log
//   超过 maxSize 自动滚动为 app.log.1
class Logger : public QObject
{
    Q_OBJECT
public:
    static Logger *instance();

    bool init(const QString &logDir, qint64 maxSize = 2 * 1024 * 1024);
    void shutdown();

    QString logFilePath() const;

private:
    explicit Logger(QObject *parent = nullptr);
    ~Logger() override;

    static void handler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    void writeLine(const QString &line);
    void rotateIfNeeded();

    QMutex  m_mutex;
    QFile   m_file;
    QString m_path;
    qint64  m_maxSize;
    bool    m_inited;
};

#endif // LOGGER_H
