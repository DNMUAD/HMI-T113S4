#ifndef PROCESSRUNNER_H
#define PROCESSRUNNER_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include "commandspec.h"

class CommandRunner;

// 设计模式 #14：外观（Facade）
//
// 硬约定「所有外部命令必须经此层」的落点。对外仍是易用的 run(program, args...)，
// 内部则退化成「组一个 CommandSpec 交给策略执行器」，并由装饰器统一打日志：
//
//     services ──> ProcessRunner（外观）──> LoggingCommandRunner（装饰）
//                                            └─> ProcessCommandRunner（QProcess 适配）
//
// 好处：服务层代码不看 QProcess、不看 job 生命周期、不写 waitFor*，
// 换执行器（真实 / 模拟）只改一行注入。
class ProcessRunner : public QObject
{
    Q_OBJECT

public:
    static ProcessRunner *instance();

    // 依赖注入点：启动时可换成 SimulatedCommandRunner 等
    void installBackend(CommandRunner *backend);
    CommandRunner *backend();
    QString backendName() const;

    void setDefaultTimeout(int ms);
    int  defaultTimeout() const;

    // 参数化执行（推荐，避免 shell 注入）；返回 jobId，可被 finished/failed 关联。
    int run(const QString &program, const QStringList &args = QStringList(),
            int timeoutMs = 0, const QByteArray &stdinData = QByteArray());

    // 仅在必须走管道/重定向时使用；服务层不要用它拼接用户输入。
    int runScript(const QString &shellCommand, int timeoutMs = 0);

    // 建造者产出的命令描述
    int submit(const CommandSpec &spec);

    bool isRunning(int jobId) const;
    int  runningCount() const;

public slots:
    void cancel(int jobId);
    void cancelAll();

signals:
    void started(int jobId, const QString &command);
    void output(int jobId, const QString &chunk);                       // 实时增量
    void finished(int jobId, int exitCode, int exitStatus, const QString &fullOutput);
    void failed(int jobId, const QString &reason);

private:
    explicit ProcessRunner(QObject *parent = nullptr);

    void attach(CommandRunner *backend);
    void ensureBackend();

    CommandRunner *m_backend;
    int            m_defaultTimeout;
};

#endif // PROCESSRUNNER_H
