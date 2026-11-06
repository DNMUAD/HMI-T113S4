#ifndef HMI_CORE_COMMAND_H
#define HMI_CORE_COMMAND_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include "commandrunner.h"
#include "commandspec.h"

// 设计模式 #5：命令（Command）
//
// 把「一次硬件动作」封装成对象：它有描述、能被排进队列、能被组合、能被取消。
// 页面因此从「拼命令 + 追踪 jobId + 判成败」里解放出来，只负责 new 一条命令。
class Command
{
public:
    explicit Command(const QString &description = QString());
    virtual ~Command();

    QString description() const;
    void    setDescription(const QString &text);

    // 提交执行（异步，不阻塞）；结果由 CommandSequence / CommandInvoker 汇总
    virtual void execute(CommandRunner *runner) = 0;
    // 正在执行的 jobId，-1 表示尚未提交
    virtual int  pendingJob() const;

private:
    QString m_description;
};

// 叶子命令：一条参数化命令或一条 shell 命令
class ShellCommand : public Command
{
public:
    explicit ShellCommand(const CommandSpec &spec, const QString &description = QString());

    void execute(CommandRunner *runner) override;
    int  pendingJob() const override;

    const CommandSpec &spec() const;

private:
    CommandSpec m_spec;
    int         m_jobId;
};

// 设计模式 #6：组合（Composite）
//
// 一串命令按顺序执行，任一步失败即中断 —— 对外和单条命令一样只是「一条命令」。
// 典型用法：以太网「一键联网」= 网卡 UP → DHCP → Ping 验证。
class CommandSequence : public QObject, public Command
{
    Q_OBJECT
public:
    explicit CommandSequence(const QString &description = QString(), QObject *parent = nullptr);
    ~CommandSequence();

    CommandSequence *add(Command *child);      // 接管所有权，返回自身便于链式拼装
    int  count() const;
    void execute(CommandRunner *runner) override;
    int  pendingJob() const override;
    void abort();

signals:
    void stepStarted(int index, const QString &description);
    void stepFinished(int index, bool ok);
    void finished(bool ok, const QString &summary);

private slots:
    void onRunnerFinished(int jobId, int exitCode, int exitStatus, const QString &output);

private:
    void startNext();
    void finishSequence(bool ok, const QString &summary);

    QList<Command *>     m_children;
    QPointer<CommandRunner> m_runner;
    int  m_index;
    bool m_connected;
    bool m_done;
    bool m_aborted;
};

#endif // HMI_CORE_COMMAND_H
