#include "loggingcommandrunner.h"
#include "logpanel.h"

#include <QTimer>

LoggingCommandRunner::LoggingCommandRunner(CommandRunner *inner, QObject *parent)
    : CommandRunner(parent)
    , m_inner(inner)
{
    if (m_inner) {
        m_inner->setParent(this);
        connect(m_inner, &CommandRunner::started, this, &CommandRunner::started);
        connect(m_inner, &CommandRunner::output, this, &CommandRunner::output);
        connect(m_inner, &CommandRunner::failed, this, &CommandRunner::failed);
        connect(m_inner, &CommandRunner::finished, this, &LoggingCommandRunner::onFinished);
    }
}

LoggingCommandRunner::~LoggingCommandRunner()
{
}

int LoggingCommandRunner::submit(const CommandSpec &spec)
{
    if (!m_inner) {
        LogPanel::post(QStringLiteral("命令执行器未安装，丢弃命令: %1").arg(spec.display()),
                       LogPanel::Error);
        return -1;
    }

    const int id = m_inner->submit(spec);
    if (id < 0)
        return id;

    m_started.insert(id, QElapsedTimer());
    m_started[id].start();
    m_quiet.insert(id, spec.isQuiet());

    LogPanel::post(QStringLiteral("$ %1").arg(spec.display()), LogPanel::Debug);
    return id;
}

bool LoggingCommandRunner::isRunning(int jobId) const
{
    return m_inner ? m_inner->isRunning(jobId) : false;
}

int LoggingCommandRunner::runningCount() const
{
    return m_inner ? m_inner->runningCount() : 0;
}

void LoggingCommandRunner::cancel(int jobId)
{
    if (m_inner)
        m_inner->cancel(jobId);
}

void LoggingCommandRunner::cancelAll()
{
    if (m_inner)
        m_inner->cancelAll();
}

QString LoggingCommandRunner::backendName() const
{
    return m_inner ? QStringLiteral("logging(%1)").arg(m_inner->backendName())
                   : QStringLiteral("logging(none)");
}

void LoggingCommandRunner::onFinished(int jobId, int exitCode, int exitStatus,
                                      const QString &fullOutput)
{
    const qint64 elapsed = m_started.contains(jobId) ? m_started.take(jobId).elapsed() : -1;
    const bool quiet = m_quiet.take(jobId);
    const bool ok = (exitCode == 0 && exitStatus == 0);

    if (!ok) {
        // 失败必须把 stderr（MergedChannels 已并入）完整打进日志
        LogPanel::post(QStringLiteral("命令失败 (job #%1, exit=%2, %3ms)%4\n%5")
                       .arg(jobId)
                       .arg(exitCode)
                       .arg(elapsed)
                       .arg(quiet ? QStringLiteral(" [quiet]") : QString())
                       .arg(fullOutput.isEmpty() ? QStringLiteral("(无输出)") : fullOutput),
                       quiet ? LogPanel::Debug : LogPanel::Error);
    } else if (!fullOutput.isEmpty()) {
        LogPanel::post(fullOutput, LogPanel::Info);
    }

    emit finished(jobId, exitCode, exitStatus, fullOutput);
}
