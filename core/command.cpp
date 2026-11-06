#include "command.h"
#include "commandrunner.h"

Command::Command(const QString &description)
    : m_description(description)
{
}

Command::~Command()
{
}

QString Command::description() const
{
    return m_description;
}

void Command::setDescription(const QString &text)
{
    m_description = text;
}

int Command::pendingJob() const
{
    return -1;
}

ShellCommand::ShellCommand(const CommandSpec &spec, const QString &description)
    : Command(description.isEmpty() ? spec.display() : description)
    , m_spec(spec)
    , m_jobId(-1)
{
}

void ShellCommand::execute(CommandRunner *runner)
{
    m_jobId = runner ? runner->submit(m_spec) : -1;
}

int ShellCommand::pendingJob() const
{
    return m_jobId;
}

const CommandSpec &ShellCommand::spec() const
{
    return m_spec;
}

CommandSequence::CommandSequence(const QString &description, QObject *parent)
    : QObject(parent)
    , Command(description)
    , m_index(-1)
    , m_connected(false)
    , m_done(false)
    , m_aborted(false)
{
}

CommandSequence::~CommandSequence()
{
    qDeleteAll(m_children);
    m_children.clear();
}

CommandSequence *CommandSequence::add(Command *child)
{
    if (child)
        m_children.append(child);
    return this;
}

int CommandSequence::count() const
{
    return m_children.size();
}

void CommandSequence::execute(CommandRunner *runner)
{
    m_runner = runner;
    m_index = -1;
    m_done = false;
    m_aborted = false;

    if (!m_runner) {
        finishSequence(false, tr("没有可用的命令执行器"));
        return;
    }
    if (!m_connected) {
        connect(m_runner.data(), &CommandRunner::finished,
                this, &CommandSequence::onRunnerFinished);
        m_connected = true;
    }
    startNext();
}

int CommandSequence::pendingJob() const
{
    if (m_index < 0 || m_index >= m_children.size())
        return -1;
    return m_children.at(m_index)->pendingJob();
}

void CommandSequence::abort()
{
    if (m_done)
        return;
    m_aborted = true;
    const int job = pendingJob();
    if (m_runner && job >= 0)
        m_runner->cancel(job);
    finishSequence(false, tr("已取消"));
}

void CommandSequence::startNext()
{
    ++m_index;
    if (m_index >= m_children.size()) {
        finishSequence(true, tr("全部完成"));
        return;
    }
    Command *child = m_children.at(m_index);
    emit stepStarted(m_index, child->description());
    child->execute(m_runner.data());
}

void CommandSequence::onRunnerFinished(int jobId, int exitCode, int exitStatus,
                                       const QString &output)
{
    Q_UNUSED(output);
    if (m_done || jobId != pendingJob())
        return;

    const bool ok = (exitCode == 0 && exitStatus == 0 && !m_aborted);
    emit stepFinished(m_index, ok);

    if (!ok) {
        finishSequence(false, tr("第 %1 步失败：%2")
                              .arg(m_index + 1)
                              .arg(m_children.at(m_index)->description()));
        return;
    }
    startNext();
}

void CommandSequence::finishSequence(bool ok, const QString &summary)
{
    if (m_done)
        return;
    m_done = true;
    emit finished(ok, summary);
}
