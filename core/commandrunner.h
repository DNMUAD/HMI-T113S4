#ifndef HMI_CORE_COMMANDRUNNER_H
#define HMI_CORE_COMMANDRUNNER_H

#include <QObject>

#include "commandspec.h"

// 设计模式 #3：策略（Strategy）
//
// 「怎么执行一条外部命令」被抽成接口，运行期注入具体实现：
//   · ProcessCommandRunner   —— 真起 QProcess（板端）
//   · SimulatedCommandRunner —— 桌面无硬件时返回预置输出（空对象 / 桩）
//   · LoggingCommandRunner   —— 装饰另一个策略，统一打日志与计时
//
// 上层（服务层、页面层）只认 CommandRunner，不关心背后是真进程还是模拟器，
// 于是「换实现不换调用方」，桌面调试与板端运行共用同一套业务代码。

class CommandRunner : public QObject
{
    Q_OBJECT
public:
    explicit CommandRunner(QObject *parent = nullptr);
    virtual ~CommandRunner();

    // 提交即返回 jobId，绝不阻塞；结果通过 finished/failed 信号回送。
    virtual int  submit(const CommandSpec &spec) = 0;
    virtual bool isRunning(int jobId) const = 0;
    virtual int  runningCount() const = 0;
    virtual void cancel(int jobId) = 0;
    virtual void cancelAll() = 0;
    virtual QString backendName() const = 0;

signals:
    void started(int jobId, const QString &command);
    void output(int jobId, const QString &chunk);
    void finished(int jobId, int exitCode, int exitStatus, const QString &fullOutput);
    void failed(int jobId, const QString &reason);
};

#endif // HMI_CORE_COMMANDRUNNER_H
