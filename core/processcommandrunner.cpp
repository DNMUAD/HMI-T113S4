#include "processcommandrunner.h"

#include <QTimer>

ProcessCommandRunner::ProcessCommandRunner(QObject *parent)
    : CommandRunner(parent)
    , m_nextId(0)
{
}

int ProcessCommandRunner::submit(const CommandSpec &spec)
{
    if (spec.programName().isEmpty())
        return -1;

    // 硬约定：3–5s 上限，宁可失败也不许阻塞 UI
    const int ms = spec.timeoutMs() > 0 ? qBound(3000, spec.timeoutMs(), 5000) : 4000;
    const int id = ++m_nextId;

    QProcess *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);   // stdout + stderr 一起收

    Job job;
    job.proc = proc;
    job.command = spec.display();
    m_jobs.insert(id, job);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, id]() { onReadyRead(id); });
    connect(proc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this, id](int code, QProcess::ExitStatus st) { finishJob(id, code, st); });
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    connect(proc, &QProcess::errorOccurred, this,
            [this, id](QProcess::ProcessError e) { onProcessError(id, e); });
#endif

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id]() { onTimeout(id); });
    m_jobs[id].timer = timer;

    emit started(id, job.command);

    if (spec.isShellCommand()) {
        proc->start(QStringLiteral("/bin/sh"),
                    QStringList() << QStringLiteral("-c") << spec.programName(),
                    QIODevice::ReadWrite);
    } else {
        proc->start(spec.programName(), spec.arguments(), QIODevice::ReadWrite);
    }

    if (!spec.stdinData().isEmpty()) {
        proc->write(spec.stdinData());
        proc->closeWriteChannel();
    }

    timer->start(ms);      // 不 waitFor*，靠信号 + 定时器收尾
    return id;
}

bool ProcessCommandRunner::isRunning(int jobId) const
{
    return m_jobs.contains(jobId);
}

int ProcessCommandRunner::runningCount() const
{
    return m_jobs.size();
}

QString ProcessCommandRunner::backendName() const
{
    return QStringLiteral("process");
}

void ProcessCommandRunner::onReadyRead(int jobId)
{
    QHash<int, Job>::Iterator it = m_jobs.find(jobId);
    if (it == m_jobs.end() || !it->proc)
        return;
    const QByteArray chunk = it->proc->readAllStandardOutput();
    if (chunk.isEmpty())
        return;
    it->buffer.append(chunk);
    emit output(jobId, QString::fromLocal8Bit(chunk));
}

void ProcessCommandRunner::onProcessError(int jobId, QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    // FailedToStart 不保证触发 finished，主动收尾，避免 job 泄漏到超时。
    finishJob(jobId, -1, QProcess::NormalExit,
              QStringLiteral("命令启动失败（程序不存在或无执行权限）"));
}

void ProcessCommandRunner::onTimeout(int jobId)
{
    QHash<int, Job>::Iterator it = m_jobs.find(jobId);
    if (it == m_jobs.end())
        return;
    it->timedOut = true;
    if (it->proc)
        it->proc->kill();
    // kill 未按时退出时的兜底，finishJob 自带去重。
    QTimer::singleShot(1000, this, [this, jobId]() {
        finishJob(jobId, -1, QProcess::CrashExit, QStringLiteral("执行超时（3–5s 上限）"));
    });
}

void ProcessCommandRunner::finishJob(int jobId, int exitCode, QProcess::ExitStatus status,
                                     const QString &errorText)
{
    if (!m_jobs.contains(jobId))
        return;
    Job job = m_jobs.take(jobId);

    if (job.timer) {
        job.timer->stop();
        job.timer->deleteLater();
    }
    if (job.proc) {
        job.buffer.append(job.proc->readAllStandardOutput());
        job.proc->blockSignals(true);
        job.proc->deleteLater();
    }

    const QString full = QString::fromLocal8Bit(job.buffer).trimmed();
    const bool crashed = (status == QProcess::CrashExit);
    const bool ok = !job.timedOut && errorText.isEmpty() && exitCode == 0 && !crashed;

    // 只负责「判定与上报」，日志交给 LoggingCommandRunner 装饰器统一处理。
    if (!ok) {
        QString reason = errorText;
        if (reason.isEmpty())
            reason = crashed ? QStringLiteral("进程异常退出") : QStringLiteral("非零退出码");
        reason += QStringLiteral("，exit=") + QString::number(exitCode);
        emit failed(jobId, reason);
    }

    emit finished(jobId, exitCode, int(status), full);
}

void ProcessCommandRunner::cancel(int jobId)
{
    QHash<int, Job>::Iterator it = m_jobs.find(jobId);
    if (it == m_jobs.end())
        return;
    if (it->proc && it->proc->state() != QProcess::NotRunning)
        it->proc->kill();
}

void ProcessCommandRunner::cancelAll()
{
    const QList<int> ids = m_jobs.keys();
    for (int id : ids)
        cancel(id);
}
