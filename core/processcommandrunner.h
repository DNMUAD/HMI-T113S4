#ifndef HMI_CORE_PROCESSCOMMANDRUNNER_H
#define HMI_CORE_PROCESSCOMMANDRUNNER_H

#include <QHash>
#include <QProcess>

#include "commandrunner.h"

class QTimer;

// 设计模式：适配器（Adapter）
//
// 把 QProcess 的异步事件模型（readyRead / finished / errorOccurred + 定时器）
// 适配成统一、简单的一小块：submit(spec) → finished(jobId, code, out)。
// 上层因此看不到 QProcess 的生命周期细节，也不需要 waitFor* 阻塞。
class ProcessCommandRunner : public CommandRunner
{
    Q_OBJECT
public:
    explicit ProcessCommandRunner(QObject *parent = nullptr);

    int  submit(const CommandSpec &spec) override;
    bool isRunning(int jobId) const override;
    int  runningCount() const override;
    void cancel(int jobId) override;
    void cancelAll() override;
    QString backendName() const override;

private:
    struct Job
    {
        QProcess  *proc;
        QTimer    *timer;
        QByteArray buffer;
        QString    command;
        bool       timedOut;

        Job() : proc(nullptr), timer(nullptr), timedOut(false) {}
    };

    void onReadyRead(int jobId);
    void onTimeout(int jobId);
    void onProcessError(int jobId, QProcess::ProcessError error);
    void finishJob(int jobId, int exitCode, QProcess::ExitStatus status,
                   const QString &errorText = QString());

    QHash<int, Job> m_jobs;
    int             m_nextId;
};

#endif // HMI_CORE_PROCESSCOMMANDRUNNER_H
