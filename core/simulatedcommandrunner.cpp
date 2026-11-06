#include "simulatedcommandrunner.h"

#include <QTimer>

SimulatedCommandRunner::SimulatedCommandRunner(QObject *parent)
    : CommandRunner(parent)
    , m_nextId(0)
{
}

int SimulatedCommandRunner::submit(const CommandSpec &spec)
{
    const int id = ++m_nextId;
    m_jobs.insert(id, true);

    const QString command = spec.display();
    const QString output = simulatedOutput(spec);

    emit started(id, command);

    // 用定时器模拟异步返回，绝不同步阻塞 —— 与真实执行器行为一致。
    QTimer::singleShot(120, this, [this, id, output]() {
        if (!m_jobs.contains(id))
            return;                     // 已被 cancel
        m_jobs.remove(id);
        emit finished(id, 0, 0, output);
    });
    return id;
}

bool SimulatedCommandRunner::isRunning(int jobId) const
{
    return m_jobs.contains(jobId);
}

int SimulatedCommandRunner::runningCount() const
{
    return m_jobs.size();
}

void SimulatedCommandRunner::cancel(int jobId)
{
    if (m_jobs.remove(jobId) > 0)
        emit failed(jobId, QStringLiteral("已取消（模拟）"));
}

void SimulatedCommandRunner::cancelAll()
{
    const QList<int> ids = m_jobs.keys();
    for (int id : ids)
        cancel(id);
}

QString SimulatedCommandRunner::backendName() const
{
    return QStringLiteral("simulated");
}

QString SimulatedCommandRunner::simulatedOutput(const CommandSpec &spec) const
{
    const QString line = spec.programName() + QLatin1Char(' ') + spec.arguments().join(QLatin1Char(' '));
    const QString name = spec.programName();

    if (line.contains(QStringLiteral("wifi -s")))
        return QStringLiteral(
            "BSS 3c:cd:5d:11:22:33 (on wlan0)\n"
            "        freq: 2437\n"
            "        signal: -42 dBm\n"
            "        SSID: T113-Demo-2G\n"
            "        RSN: * Version: 1\n"
            "BSS 3c:cd:5d:44:55:66 (on wlan0)\n"
            "        freq: 5180\n"
            "        signal: -63 dBm\n"
            "        SSID: Office-5G\n"
            "        RSN: * Version: 1\n"
            "BSS 3c:cd:5d:77:88:99 (on wlan0)\n"
            "        freq: 2412\n"
            "        signal: -78 dBm\n"
            "        SSID: Guest-Open\n");

    if (line.contains(QStringLiteral("iw dev wlan0 scan")))
        return simulatedOutput(CommandSpec(QStringLiteral("wifi")).arg(QStringLiteral("-s")));

    if (line.contains(QStringLiteral("addr show wlan0")))
        return QStringLiteral("3: wlan0: <BROADCAST,MULTICAST,UP> mtu 1500\n"
                              "    inet 192.168.100.23/24 brd 192.168.100.255 scope global wlan0\n");

    if (line.contains(QStringLiteral("addr show eth0")))
        return QStringLiteral("2: eth0: <BROADCAST,MULTICAST,UP> mtu 1500\n"
                              "    inet 10.0.0.8/24 brd 10.0.0.255 scope global eth0\n");

    if (name == QStringLiteral("ping"))
        return QStringLiteral("4 packets transmitted, 4 received, 0% packet loss\n"
                              "rtt min/avg/max = 12.1/18.4/25.9 ms");

    if (name == QStringLiteral("udhcpc"))
        return QStringLiteral("udhcpc: lease of 10.0.0.8 obtained, lease time 7200");

    if (name == QStringLiteral("ifconfig"))
        return QStringLiteral("eth0      Link encap:Ethernet  HWaddr 02:00:00:12:34:56");

    if (name == QStringLiteral("pidof"))
        return QStringLiteral("4321");

    if (name == QStringLiteral("hciconfig"))
        return QStringLiteral("hci0:   Type: Primary  Bus: UART\n"
                              "        BD Address: 11:22:33:44:55:66  ACL MTU: 1021:8\n"
                              "        UP RUNNING");

    if (name == QStringLiteral("uname"))
        return QStringLiteral("Linux t113-s4 5.4.61 #1 SMP PREEMPT armv7l GNU/Linux");

    if (name == QStringLiteral("ip") && line.contains(QStringLiteral("-d link")))
        return QStringLiteral("1: lo: <LOOPBACK,UP> mtu 65536\n"
                              "3: awlink0: <NOARP,UP> mtu 16 qdisc noqueue\n"
                              "    link/can  can <LOOPBACK> state ERROR-ACTIVE\n");

    return QStringLiteral("(模拟执行) %1\n返回码 0，桌面无硬件时以预置结果代替。").arg(line);
}
