#include "commandspec.h"

CommandSpec::CommandSpec()
    : m_timeout(0)
    , m_shell(false)
    , m_quiet(false)
{
}

CommandSpec::CommandSpec(const QString &program)
    : m_program(program)
    , m_timeout(0)
    , m_shell(false)
    , m_quiet(false)
{
}

CommandSpec &CommandSpec::program(const QString &program)
{
    m_program = program;
    return *this;
}

CommandSpec &CommandSpec::arg(const QString &value)
{
    m_args << value;
    return *this;
}

CommandSpec &CommandSpec::args(const QStringList &values)
{
    m_args << values;
    return *this;
}

CommandSpec &CommandSpec::timeout(int ms)
{
    m_timeout = ms;
    return *this;
}

CommandSpec &CommandSpec::input(const QByteArray &data)
{
    m_input = data;
    return *this;
}

CommandSpec &CommandSpec::describe(const QString &text)
{
    m_description = text;
    return *this;
}

CommandSpec &CommandSpec::viaShell(bool enabled)
{
    m_shell = enabled;
    return *this;
}

CommandSpec &CommandSpec::quiet(bool enabled)
{
    m_quiet = enabled;
    return *this;
}

QString CommandSpec::programName() const
{
    return m_program;
}

QStringList CommandSpec::arguments() const
{
    return m_args;
}

bool CommandSpec::isShellCommand() const
{
    return m_shell;
}

int CommandSpec::timeoutMs() const
{
    return m_timeout;
}

QByteArray CommandSpec::stdinData() const
{
    return m_input;
}

QString CommandSpec::description() const
{
    return m_description;
}

bool CommandSpec::isQuiet() const
{
    return m_quiet;
}

QString CommandSpec::display() const
{
    if (m_program.isEmpty())
        return QString();

    // 走 shell 时 m_program 本身就是整条命令串
    if (m_shell)
        return QStringLiteral("/bin/sh -c \"") + m_program + QLatin1Char('"');

    QStringList parts;
    parts << m_program;
    for (int i = 0; i < m_args.size(); ++i) {
        const QString &a = m_args.at(i);
        parts << (a.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(a) : a);
    }
    return parts.join(QLatin1Char(' '));
}
