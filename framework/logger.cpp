#include "logger.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>

#include <cstdio>

namespace {
Logger *g_logger = nullptr;
}

Logger *Logger::instance()
{
    if (!g_logger)
        g_logger = new Logger;
    return g_logger;
}

Logger::Logger(QObject *parent)
    : QObject(parent)
    , m_maxSize(2 * 1024 * 1024)
    , m_inited(false)
{
    g_logger = this;
}

Logger::~Logger()
{
    shutdown();
}

bool Logger::init(const QString &logDir, qint64 maxSize)
{
    QMutexLocker locker(&m_mutex);

    m_maxSize = maxSize;

    QDir dir(logDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    m_path = dir.filePath(QStringLiteral("app.log"));
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;

    m_inited = true;
    qInstallMessageHandler(&Logger::handler);
    return true;
}

void Logger::shutdown()
{
    QMutexLocker locker(&m_mutex);

    if (!m_inited)
        return;

    qInstallMessageHandler(nullptr);
    m_file.flush();
    m_file.close();
    m_inited = false;
}

QString Logger::logFilePath() const
{
    return m_path;
}

void Logger::handler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context)

    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO";  break;
    case QtWarningMsg:  level = "WARN";  break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    const QString line = QStringLiteral("[%1][%2] %3")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")))
            .arg(QString::fromLatin1(level))
            .arg(msg);

    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);

    Logger::instance()->writeLine(line);

    if (type == QtFatalMsg)
        abort();
}

void Logger::writeLine(const QString &line)
{
    QMutexLocker locker(&m_mutex);

    if (!m_inited)
        return;

    rotateIfNeeded();

    m_file.write(line.toUtf8());
    m_file.write("\n");
    m_file.flush();
}

void Logger::rotateIfNeeded()
{
    if (m_file.size() < m_maxSize)
        return;

    m_file.close();

    const QString backup = m_path + QStringLiteral(".1");
    QFile::remove(backup);
    QFile::rename(m_path, backup);

    m_file.setFileName(m_path);
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}
