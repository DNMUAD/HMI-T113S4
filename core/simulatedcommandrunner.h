#ifndef HMI_CORE_SIMULATEDCOMMANDRUNNER_H
#define HMI_CORE_SIMULATEDCOMMANDRUNNER_H

#include <QHash>

#include "commandrunner.h"

// 设计模式：空对象 / 桩实现（Null Object）+ 策略（Strategy）
//
// 桌面没有 T113 板、没有 wifi/udhcpc/hciconfig 时，用它顶替真实执行器：
// 返回预置输出并照常走 finished 信号，于是整条 UI 链路（页面 → 服务 → 运行器）
// 都能在 PC 上跑通，不用给每个调用点写 #ifdef。
// 板端不会实例化它，删掉也不影响功能。

class SimulatedCommandRunner : public CommandRunner
{
    Q_OBJECT
public:
    explicit SimulatedCommandRunner(QObject *parent = nullptr);

    int  submit(const CommandSpec &spec) override;
    bool isRunning(int jobId) const override;
    int  runningCount() const override;
    void cancel(int jobId) override;
    void cancelAll() override;
    QString backendName() const override;

    QString simulatedOutput(const CommandSpec &spec) const;

private:
    QHash<int, bool> m_jobs;
    int              m_nextId;
};

#endif // HMI_CORE_SIMULATEDCOMMANDRUNNER_H
