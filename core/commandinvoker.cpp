#include "commandinvoker.h"
#include "commandrunner.h"
#include "logpanel.h"
#include "singleton.h"

CommandInvoker *CommandInvoker::instance()
{
    return hmi::singleton<CommandInvoker>([]() { return new CommandInvoker; });
}

CommandInvoker::CommandInvoker(QObject *parent)
    : QObject(parent)
    , m_runner(nullptr)
    , m_active(nullptr)
{
}

void CommandInvoker::setRunner(CommandRunner *runner)
{
    m_runner = runner;
}

CommandRunner *CommandInvoker::runner() const
{
    return m_runner;
}

void CommandInvoker::enqueue(CommandSequence *sequence)
{
    if (!sequence)
        return;

    m_queue.enqueue(sequence);
    if (!m_active)
        startNext();
}

bool CommandInvoker::isBusy() const
{
    return m_active != nullptr;
}

int CommandInvoker::queuedCount() const
{
    return m_queue.size();
}

QString CommandInvoker::activeDescription() const
{
    return m_active ? m_active->description() : QString();
}

void CommandInvoker::cancelCurrent()
{
    if (m_active)
        m_active->abort();
    while (!m_queue.isEmpty())
        m_queue.dequeue()->deleteLater();
    emit busyChanged(m_active != nullptr);
}

void CommandInvoker::startNext()
{
    if (m_active || m_queue.isEmpty()) {
        emit busyChanged(m_active != nullptr);
        return;
    }

    m_active = m_queue.dequeue();
    connect(m_active, &CommandSequence::finished, this, &CommandInvoker::onSequenceFinished);

    LogPanel::post(tr("▶ 开始执行命令序列：%1（共 %2 步）")
                   .arg(m_active->description()).arg(m_active->count()), LogPanel::Ok);
    emit sequenceStarted(m_active->description());
    emit busyChanged(true);

    m_active->execute(m_runner);
}

void CommandInvoker::onSequenceFinished(bool ok, const QString &summary)
{
    CommandSequence *done = m_active;
    m_active = nullptr;

    LogPanel::post(ok ? tr("✔ 命令序列完成：%1 · %2").arg(done->description(), summary)
                      : tr("✘ 命令序列失败：%1 · %2").arg(done->description(), summary),
                   ok ? LogPanel::Ok : LogPanel::Error);
    emit sequenceFinished(done->description(), ok, summary);

    done->deleteLater();
    startNext();
}
