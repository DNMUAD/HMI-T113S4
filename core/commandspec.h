#ifndef HMI_CORE_COMMANDSPEC_H
#define HMI_CORE_COMMANDSPEC_H

#include <QByteArray>
#include <QString>
#include <QStringList>

// 设计模式 #2：建造者（Builder）
//
// 一条外部命令有 5 个维度（程序、参数、超时、标准输入、日志描述），
// 用链式调用拼装，调用点读起来就是命令本身：
//
//     CommandSpec(QStringLiteral("wifi")).arg("-c").arg(ssid).arg(pwd)
//                 .timeout(4000).describe(tr("连接 WiFi"));
//
// 好处：① 不再出现 run(prog, args, timeout, stdin) 四个位置参数传错；
//       ② 参数化传参，不拼接 shell 字符串，天然免疫注入；
//       ③ describe/quiet 这类「元信息」可以随命令一起流转到日志装饰器。

class CommandSpec
{
public:
    CommandSpec();
    explicit CommandSpec(const QString &program);

    CommandSpec &program(const QString &program);
    CommandSpec &arg(const QString &value);
    CommandSpec &args(const QStringList &values);
    CommandSpec &timeout(int ms);
    CommandSpec &input(const QByteArray &data);
    CommandSpec &describe(const QString &text);
    CommandSpec &viaShell(bool enabled = true);
    // 预期会失败的探测命令（如 pidof），失败只记 Debug，不刷 Error
    CommandSpec &quiet(bool enabled = true);

    QString     programName() const;
    QStringList arguments() const;
    bool        isShellCommand() const;
    int         timeoutMs() const;
    QByteArray  stdinData() const;
    QString     description() const;
    bool        isQuiet() const;
    QString     display() const;

private:
    QString     m_program;
    QStringList m_args;
    int         m_timeout;
    QByteArray  m_input;
    QString     m_description;
    bool        m_shell;
    bool        m_quiet;
};

#endif // HMI_CORE_COMMANDSPEC_H
