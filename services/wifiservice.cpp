#include "wifiservice.h"
#include "command.h"
#include "logpanel.h"
#include "processrunner.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QRegularExpression>

namespace {
QString firstInet(const QString &out)
{
    QRegularExpression rx(QStringLiteral("\\binet\\s+(\\d+\\.\\d+\\.\\d+\\.\\d+)"));
    const QRegularExpressionMatch m = rx.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}
} // namespace

WifiService *WifiService::instance()
{
    return hmi::singleton<WifiService>([]() { return new WifiService; });
}

WifiService::WifiService(QObject *parent)
    : DeviceService(QStringLiteral("wifi"), QObject::tr("WiFi"), 5000, parent)
    , m_scanJob(-1)
    , m_statusJob(-1)
    , m_actionJob(-1)
    , m_scanFallbackDone(false)
{
    connect(ProcessRunner::instance(), &ProcessRunner::finished,
            this, &WifiService::onFinished);
    connect(ProcessRunner::instance(), &ProcessRunner::failed,
            this, &WifiService::onFailed);
}

void WifiService::initSta()
{
    publish(DeviceBusy, tr("初始化 STA 模式"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                  QStringList() << QStringLiteral("-o") << QStringLiteral("sta"));
}

void WifiService::initAp()
{
    publish(DeviceBusy, tr("切换 AP 模式"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                  QStringList() << QStringLiteral("-o") << QStringLiteral("ap"));
}

void WifiService::scan()
{
    m_scanFallbackDone = false;
    publish(DeviceBusy, tr("扫描中"));
    m_scanJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                QStringList() << QStringLiteral("-s"));
}

void WifiService::connectTo(const QString &ssid, const QString &password)
{
    if (ssid.trimmed().isEmpty()) {
        LogPanel::post(QStringLiteral("SSID 为空，取消连接"), LogPanel::Warn);
        return;
    }
    publish(DeviceBusy, tr("连接 %1").arg(ssid));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                  QStringList() << QStringLiteral("-c") << ssid << password);
    refresh();
}

void WifiService::disconnect()
{
    publish(DeviceBusy, tr("断开连接"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                  QStringList() << QStringLiteral("-d"));
    refresh();
}

void WifiService::refresh()
{
    m_statusJob = ProcessRunner::instance()->run(QStringLiteral("ip"),
                                                  QStringList() << QStringLiteral("-4") << QStringLiteral("addr")
                                                                << QStringLiteral("show") << QStringLiteral("wlan0"));
}

CommandSequence *WifiService::makeConnectSequence(const QString &ssid, const QString &password)
{
    CommandSequence *sequence = new CommandSequence(tr("WiFi 一键联网：%1").arg(ssid));
    sequence->add(new ShellCommand(CommandSpec(QStringLiteral("wifi"))
                                   .arg(QStringLiteral("-o")).arg(QStringLiteral("sta")),
                                   tr("初始化 STA 模式")))
            ->add(new ShellCommand(CommandSpec(QStringLiteral("wifi"))
                                   .arg(QStringLiteral("-c")).arg(ssid).arg(password),
                                   tr("连接 %1").arg(ssid)))
            ->add(new ShellCommand(CommandSpec(QStringLiteral("ip"))
                                   .arg(QStringLiteral("-4")).arg(QStringLiteral("addr"))
                                   .arg(QStringLiteral("show")).arg(QStringLiteral("wlan0")),
                                   tr("读取 IP")));
    return sequence;
}

void WifiService::onFinished(int jobId, int exitCode, int, const QString &out)
{
    if (jobId == m_scanJob) {
        m_scanJob = -1;
        emit scanResult(out);
        refresh();
        return;
    }
    if (jobId == m_statusJob) {
        m_statusJob = -1;
        const QString ip = firstInet(out);
        const bool up = (exitCode == 0 && !ip.isEmpty());
        publish(up ? DeviceReady : DeviceOff, up ? ip : tr("未获取 IP"));
        return;
    }
    if (jobId == m_actionJob) {
        m_actionJob = -1;
        refresh();
    }
}

void WifiService::onFailed(int jobId, const QString &reason)
{
    if (jobId == m_scanJob && !m_scanFallbackDone) {
        // 硬约定：wifi -s 不可用时回退 iw dev wlan0 scan
        m_scanFallbackDone = true;
        LogPanel::post(QStringLiteral("wifi -s 不可用，回退 iw dev wlan0 scan"), LogPanel::Warn);
        m_scanJob = ProcessRunner::instance()->run(QStringLiteral("iw"),
                                                    QStringList() << QStringLiteral("dev") << QStringLiteral("wlan0")
                                                                  << QStringLiteral("scan"));
        return;
    }
    if (jobId == m_statusJob) {
        m_statusJob = -1;
        publish(DeviceOff, reason);
        return;
    }
    if (jobId == m_actionJob) {
        m_actionJob = -1;
        publish(DeviceError, reason);
    }
}

// 自注册：注册表据此做「全部刷新」与周期探测
HMI_REGISTER_SERVICE("wifi", WifiService)
