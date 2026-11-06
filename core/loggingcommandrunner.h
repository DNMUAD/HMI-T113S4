#ifndef HMI_CORE_LOGGINGCOMMANDRUNNER_H
#define HMI_CORE_LOGGINGCOMMANDRUNNER_H

#include <QElapsedTimer>
#include <QHash>

#include "commandrunner.h"

// 设计模式 #4：装饰器（Decorator）
//
// 在执行策略外面套一层，只加「统一日志 + 计时」，不改被装饰者的代码：
//
//     runner = new LoggingCommandRunner(new ProcessCommandRunner);
//
// 于是「所有外部命令的 stdout/stderr 必须进 LogPanel」这条硬约定
// 只有一处实现，换成模拟执行器时日志行为自动继承。

class LoggingCommandRunner : public CommandRunner
{
    Q_OBJECT
public:
    explicit LoggingCommandRunner(CommandRunner *inner, QObject *parent = nullptr);
    ~LoggingCommandRunner();

    int  submit(const CommandSpec &spec) override;
    bool isRunning(int jobId) const override;
    int  runningCount() const override;
    void cancel(int jobId) override;
    void cancelAll() override;
    QString backendName() const override;

private slots:
    void onFinished(int jobId, int exitCode, int exitStatus, const QString &fullOutput);

private:
    CommandRunner *m_inner;
    QHash<int, QElapsedTimer> m_started;
    QHash<int, bool>          m_quiet;
};

#endif // HMI_CORE_LOGGINGCOMMANDRUNNER_H
