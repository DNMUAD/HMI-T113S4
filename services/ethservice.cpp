#include "ethservice.h"
#include "command.h"
#include "logpanel.h"
#include "processrunner.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QFile>
#include <QRegularExpression>

namespace {
QString firstInet(const QString &out)
{
    QRegularExpression rx(QStringLiteral("\\binet\\s+(\\d+\\.\\d+\\.\\d+\\.\\d+)"));
    const QRegularExpressionMatch m = rx.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}
} // namespace

EthService *EthService::instance()
{
    return hmi::singleton<EthService>([]() { return new EthService; });
}

EthService::EthService(QObject *parent)
    : DeviceService(QStringLiteral("eth"), QObject::tr("以太网"), 5000, parent)
    , m_statusJob(-1)
    , m_pingJob(-1)
    , m_actionJob(-1)
{
    connect(ProcessRunner::instance(), &ProcessRunner::finished,
            this, &EthService::onFinished);
}

QString EthService::readSysfs(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll()).trimmed();
}

void EthService::readLinkInfo()
{
    const QString state = readSysfs(QStringLiteral("/sys/class/net/eth0/operstate"));
    const QString speed = readSysfs(QStringLiteral("/sys/class/net/eth0/speed"));
    emit linkInfoChanged(state.isEmpty() ? QStringLiteral("unknown") : state,
                         speed.isEmpty() ? QStringLiteral("-") : speed + QStringLiteral(" Mbps"));
}

void EthService::refresh()
{
    m_statusJob = ProcessRunner::instance()->run(QStringLiteral("ip"),
                                                  QStringList() << QStringLiteral("-4") << QStringLiteral("addr")
                                                                << QStringLiteral("show") << QStringLiteral("eth0"));
    readLinkInfo();
}

void EthService::bringUp()
{
    publish(DeviceBusy, tr("ifconfig eth0 up"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("ifconfig"),
                                                  QStringList() << QStringLiteral("eth0") << QStringLiteral("up"));
}

void EthService::dhcp()
{
    publish(DeviceBusy, tr("udhcpc 获取地址"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("udhcpc"),
                                                  QStringList() << QStringLiteral("-i") << QStringLiteral("eth0")
                                                                << QStringLiteral("-n") << QStringLiteral("-q"));
}

void EthService::pingTest()
{
    m_pingJob = ProcessRunner::instance()->run(QStringLiteral("ping"),
                                                QStringList() << QStringLiteral("-c") << QStringLiteral("4")
                                                              << QStringLiteral("-W") << QStringLiteral("1")
                                                              << QStringLiteral("www.baidu.com"), 5000);
}

CommandSequence *EthService::makeConnectSequence()
{
    CommandSequence *sequence = new CommandSequence(tr("以太网一键联网"));
    sequence->add(new ShellCommand(CommandSpec(QStringLiteral("ifconfig"))
                                   .arg(QStringLiteral("eth0")).arg(QStringLiteral("up")),
                                   tr("网卡 UP")))
            ->add(new ShellCommand(CommandSpec(QStringLiteral("udhcpc"))
                                   .arg(QStringLiteral("-i")).arg(QStringLiteral("eth0"))
                                   .arg(QStringLiteral("-n")).arg(QStringLiteral("-q"))
                                   .timeout(5000),
                                   tr("DHCP 获取地址")))
            ->add(new ShellCommand(CommandSpec(QStringLiteral("ping"))
                                   .arg(QStringLiteral("-c")).arg(QStringLiteral("1"))
                                   .arg(QStringLiteral("-W")).arg(QStringLiteral("2"))
                                   .arg(QStringLiteral("www.baidu.com"))
                                   .timeout(5000),
                                   tr("连通性验证")));
    return sequence;
}

void EthService::onFinished(int jobId, int exitCode, int, const QString &out)
{
    if (jobId == m_statusJob) {
        m_statusJob = -1;
        const QString ip = firstInet(out);
        const bool up = (exitCode == 0 && !ip.isEmpty());
        publish(up ? DeviceReady : DeviceOff,
                up ? ip : tr("链路未获取 IP"));
    } else if (jobId == m_pingJob) {
        m_pingJob = -1;
        emit pingResult(out);
    } else if (jobId == m_actionJob) {
        m_actionJob = -1;
        refresh();
    }
}

HMI_REGISTER_SERVICE("eth", EthService)
