#include "processrunner.h"
#include "commandrunner.h"
#include "loggingcommandrunner.h"
#include "logpanel.h"
#include "processcommandrunner.h"
#include "singleton.h"

ProcessRunner *ProcessRunner::instance()
{
    return hmi::singleton<ProcessRunner>([]() { return new ProcessRunner; });
}

ProcessRunner::ProcessRunner(QObject *parent)
    : QObject(parent)
    , m_backend(nullptr)
    , m_defaultTimeout(4000)
{
}

void ProcessRunner::attach(CommandRunner *backend)
{
    if (!backend || m_backend == backend)
        return;

    if (m_backend) {
        disconnect(m_backend, nullptr, this, nullptr);
        m_backend->deleteLater();
    }

    m_backend = backend;
    m_backend->setParent(this);

    connect(m_backend, &CommandRunner::started, this, &ProcessRunner::started);
    connect(m_backend, &CommandRunner::output, this, &ProcessRunner::output);
    connect(m_backend, &CommandRunner::finished, this, &ProcessRunner::finished);
    connect(m_backend, &CommandRunner::failed, this, &ProcessRunner::failed);
}

void ProcessRunner::ensureBackend()
{
    if (m_backend)
        return;
    // 默认组合：日志装饰器包住真实 QProcess 执行器
    attach(new LoggingCommandRunner(new ProcessCommandRunner));
}

void ProcessRunner::installBackend(CommandRunner *backend)
{
    if (!backend)
        return;
    attach(backend);
    LogPanel::post(QStringLiteral("命令执行器 = %1").arg(m_backend->backendName()), LogPanel::Debug);
}

CommandRunner *ProcessRunner::backend()
{
    ensureBackend();
    return m_backend;
}

QString ProcessRunner::backendName() const
{
    return m_backend ? m_backend->backendName() : QStringLiteral("none");
}

void ProcessRunner::setDefaultTimeout(int ms)
{
    m_defaultTimeout = qBound(3000, ms, 5000);   // 硬约定：3–5s
}

int ProcessRunner::defaultTimeout() const
{
    return m_defaultTimeout;
}

int ProcessRunner::run(const QString &program, const QStringList &args,
                       int timeoutMs, const QByteArray &stdinData)
{
    return submit(CommandSpec(program).args(args).timeout(timeoutMs).input(stdinData));
}

int ProcessRunner::runScript(const QString &shellCommand, int timeoutMs)
{
    return submit(CommandSpec(shellCommand).viaShell().timeout(timeoutMs));
}

int ProcessRunner::submit(const CommandSpec &spec)
{
    ensureBackend();

    CommandSpec effective = spec;
    if (effective.timeoutMs() <= 0)
        effective.timeout(m_defaultTimeout);

    return m_backend->submit(effective);
}

bool ProcessRunner::isRunning(int jobId) const
{
    return m_backend ? m_backend->isRunning(jobId) : false;
}

int ProcessRunner::runningCount() const
{
    return m_backend ? m_backend->runningCount() : 0;
}

void ProcessRunner::cancel(int jobId)
{
    if (m_backend)
        m_backend->cancel(jobId);
}

void ProcessRunner::cancelAll()
{
    if (m_backend)
        m_backend->cancelAll();
}
