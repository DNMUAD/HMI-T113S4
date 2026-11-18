#include "wifiservice.h"
#include "appsettings.h"
#include "command.h"
#include "commandinvoker.h"
#include "logpanel.h"
#include "processrunner.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTimer>

namespace {

// 开机链路脚本：装载驱动 → ifconfig up → 起 wifi_daemon（幂等）
const char kLinkScript[] = "/etc/init.d/S41wifi";

// wpa_cli scan 之后等多久再取结果。
// 实测这块板整轮扫描约 1670ms（驱动日志：41 个信道），留点余量取 2000ms。
const int kScanSettleMs = 2000;

// 扫描倒计时的默认秒数与上下限（可在 hmi.ini 的 wifi/scanSeconds 覆盖）。
const int kScanSecondsDefault = 5;
const int kScanSecondsMin = 2;
const int kScanSecondsMax = 30;

QString firstInet(const QString &out)
{
    static const QRegularExpression rx(
        QStringLiteral("\\binet\\s+(\\d+\\.\\d+\\.\\d+\\.\\d+)"));
    const QRegularExpressionMatch m = rx.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

// 进程是否在跑：读 /proc/<pid>/comm 比 fork 一个 pidof 便宜，
// 而且 5s 一次的轮询不该引入子进程。
bool processRunning(const QString &name)
{
    const QByteArray target = name.toLatin1();
    QDir proc(QStringLiteral("/proc"));
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (int i = 0; i < pids.size(); ++i) {
        const QString p = pids.at(i);
        if (p.isEmpty() || !p.at(0).isDigit())
            continue;
        QFile comm(QStringLiteral("/proc/%1/comm").arg(p));
        if (comm.open(QIODevice::ReadOnly)) {
            if (comm.readAll().trimmed() == target)
                return true;
        }
    }
    return false;
}

bool linkScriptExists()
{
    return QFile::exists(QString::fromLatin1(kLinkScript));
}

// 定位一个外部命令的绝对路径。
//
// 为什么必须这么做：本程序由 S70launcher 从 init 拉起，而 /etc/init.d/rcS
// 不设置 PATH，/etc/profile 里的 PATH（含 /usr/sbin）只对登录 shell 生效。
// 板端 wpa_cli 和 iw 都在 /usr/sbin 下 —— 一旦继承到的 PATH 不含该目录，
// QProcess 会 FailedToStart，扫描就在第一步就断了。
//
// 桌面调试时 PATH 通常没问题，所以这个 bug 在桌面上永远复现不出来，
// 只有上板才炸。按候选目录逐个查，最后回退到不带路径的名字
// （找不到时让 QProcess 报 FailedToStart，日志里能看出是哪个命令）。
QString resolveCommand(const QString &name, const QStringList &extraDirs = QStringList())
{
    if (name.contains(QLatin1Char('/')))
        return name;                    // 已经是绝对/相对路径

    QStringList dirs = extraDirs;
    dirs << QStringLiteral("/usr/sbin") << QStringLiteral("/sbin")
         << QStringLiteral("/usr/bin") << QStringLiteral("/bin")
         << QStringLiteral("/usr/local/sbin") << QStringLiteral("/usr/local/bin");
    for (int i = 0; i < dirs.size(); ++i) {
        const QString path = dirs.at(i) + QLatin1Char('/') + name;
        QFileInfo info(path);
        if (info.exists() && info.isExecutable())
            return path;
    }
    return name;                        // 交给 QProcess 报错，日志里有指向
}

// 解析 `wifi -l ap` 的输出。真实格式（sta_num 与 deviceN 行是分开打的）：
//
//   # wifi -l ap
//   # WINF: get ap config success:
//   ssid=T113S4
//   psk=12345678
//   key_mgmt=0
//   sec=4
//   channel=6
//   WINF: ap ip_addr: 192.168.5.1
//   WINF: ap gw_addr: 192.168.5.1
//   WINF: sta_num=1
//   WINF: device1: c2:e2:10:04:6a:32
//
// 没有设备连接时，sta_num=0 且没有 deviceN 行。
//
// 以 deviceN 行作为唯一依据，不拿 sta_num 推算：两者万一不一致（daemon 刚
// 摘掉一台还没刷新计数），deviceN 才是真实存在的连接。正则故意不锚定行首，
// 因为每行都带 "WINF:" 前缀，而前缀措辞可能随版本变化。
QStringList parseApClients(const QString &out)
{
    // (?<![A-Za-z0-9_-]) 这个前置断言不是可有可无的：
    // 少了它，SSID 里带 device 字样的热点（如 ssid=my-device1:11:22:33:44:55:66）
    // 会被当成一台已连接设备列进列表。断言要求 device 前面不是标识符字符，
    // 真实输出里它总是跟在 "WINF: " 的空格后面，配得上。
    static const QRegularExpression rx(
        QStringLiteral("(?<![A-Za-z0-9_-])device\\s*\\d+\\s*:\\s*"
                       "([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})"));
    QStringList macs;
    const QStringList lines = out.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = rx.match(lines.at(i));
        if (!m.hasMatch())
            continue;
        const QString mac = m.captured(1).toLower();
        if (!macs.contains(mac))
            macs.append(mac);
    }
    return macs;
}

// 取热点自身的 IP（输出里的 "WINF: ap ip_addr: 192.168.5.1"）。
// 解析不出来就返回空串，调用方退回不带 IP 的文案。
QString parseApIp(const QString &out)
{
    static const QRegularExpression rx(
        QStringLiteral("ap\\s+ip_addr\\s*:\\s*(\\d+\\.\\d+\\.\\d+\\.\\d+)"));
    const QRegularExpressionMatch m = rx.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

// MAC → IP 映射，数据来自 /proc/net/arp。
//
// 为什么不用 `ip neigh show dev <iface>`：BusyBox 的 ip applet 不一定编译了
// neigh 子命令（这块板的 busybox 连 pgrep 都没编），而 /proc/net/arp 是内核
// 固定接口，纯文件读取 —— 不起子进程、不吃 5s 超时钳制、失败也只是读到空。
//
// 只保留 flag 带 0x2（ATF_COM，已完成解析）的条目：0x0 的"未完成"条目
// 有 IP 但 MAC 是 00:00:00:00:00:00，拿它当已连接设备会误报。
QHash<QString, QString> arpTable(const QString &iface)
{
    QHash<QString, QString> map;
    QFile file(QStringLiteral("/proc/net/arp"));
    if (!file.open(QIODevice::ReadOnly))
        return map;

    static const QRegularExpression sep(QStringLiteral("\\s+"));
    const QStringList lines = QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'));
    // 第 0 行是表头：IP address / HW type / Flags / HW address / Mask / Device
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList cols = lines.at(i).split(sep, Qt::SkipEmptyParts);
        if (cols.size() < 6)
            continue;
        if (cols.at(5) != iface)          // 只看当前无线网卡
            continue;
        bool ok = false;
        const int flags = cols.at(2).toInt(&ok, 16);
        if (!ok || (flags & 0x2) == 0)
            continue;
        map.insert(cols.at(3).toLower(), cols.at(0));
    }
    return map;
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
    , m_action(ActNone)
    , m_mode(ModeSta)
    , m_scanStep(0)
    , m_scanSeq(0)
    , m_scanLeft(0)
    , m_scanTimer(new QTimer(this))
    , m_staInited(false)
    , m_pendingScanAfterSta(false)
    , m_wlan(QStringLiteral("wlan0"))
{
    m_scanTimer->setInterval(1000);
    m_scanTimer->setSingleShot(false);
    connect(m_scanTimer, &QTimer::timeout, this, &WifiService::onScanTick);

    connect(ProcessRunner::instance(), &ProcessRunner::finished,
            this, &WifiService::onFinished);
    connect(ProcessRunner::instance(), &ProcessRunner::failed,
            this, &WifiService::onFailed);
    detectInterfaces();
}

void WifiService::detectInterfaces()
{
    QDir net(QStringLiteral("/sys/class/net"));
    const QStringList names = net.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (int i = 0; i < names.size(); ++i) {
        const QString n = names.at(i);
        if (n.startsWith(QLatin1String("wlan")) || n.startsWith(QLatin1String("wlp"))) {
            m_wlan = n;
            return;
        }
    }
    for (int i = 0; i < names.size(); ++i) {
        const QString n = names.at(i);
        if (QFile::exists(QStringLiteral("/sys/class/net/%1/phy80211").arg(n))) {
            m_wlan = n;
            return;
        }
    }
    m_wlan = QStringLiteral("wlan0");
}

bool WifiService::hasWirelessNode() const
{
    return QFile::exists(QStringLiteral("/sys/class/net/") + m_wlan);
}

bool WifiService::driverReady() const
{
    return hasWirelessNode();
}

bool WifiService::daemonReady() const
{
    return processRunning(QStringLiteral("wifi_daemon"));
}

bool WifiService::linkReady() const
{
    return driverReady() && daemonReady();
}

WifiService::WifiMode WifiService::mode() const
{
    return m_mode;
}

void WifiService::setMode(WifiMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    // 切到 AP 时 STA 一定被 wifi -f sta 关掉了，标志必须清零；
    // 切回 STA 时保留原值，让 scan() 的「没切过才补切」判断继续有效
    // （modeChanged 之后页面会调 enterMode()，成功后自然会置 true）。
    if (mode == ModeAp) {
        m_staInited = false;
        m_pendingScanAfterSta = false;
    }
    emit modeChanged(mode);
}

QString WifiService::wpaCtrlDir() const
{
    const QStringList candidates = QStringList()
            << QStringLiteral("/etc/wifi/wpa_supplicant/sockets")
            << QStringLiteral("/var/run/wpa_supplicant")
            << QStringLiteral("/run/wpa_supplicant");
    for (int i = 0; i < candidates.size(); ++i) {
        QDir d(candidates.at(i));
        if (d.exists())
            return d.absolutePath();
    }
    return candidates.first();
}

int WifiService::scanSeconds() const
{
    int value = kScanSecondsDefault;
    AppSettings *s = AppSettings::instance();
    if (s) {
        const QString raw = s->value(QStringLiteral("wifi/scanSeconds"));
        if (!raw.trimmed().isEmpty()) {
            const int v = raw.toInt();
            if (v > 0)
                value = qBound(kScanSecondsMin, v, kScanSecondsMax);
        }
    }
    return value;
}

// ── 模式开启 ────────────────────────────────────────────────────
// STA：wifi -o sta
// AP ：wifi -f sta
//     wifimanager 没有「先关 STA 再开 AP」的原子命令，只有 -f sta 让位。
//     按你给的用法：切到 AP 只要执行 wifi -f sta，热点由 openAp() 单独开。
void WifiService::enterMode()
{
    if (!linkReady()) {
        if (!linkScriptExists()) {
            publish(DeviceOff, hasWirelessNode()
                                   ? tr("链路由板端开机脚本提供，当前环境无此脚本")
                                   : tr("未检测到无线网卡"));
            return;
        }
        LogPanel::post(tr("链路未就绪（%1=%2，wifi_daemon=%3），重跑 %4 start")
                           .arg(m_wlan,
                                hasWirelessNode() ? tr("有") : tr("无"),
                                daemonReady() ? tr("运行中") : tr("未运行"),
                                QString::fromLatin1(kLinkScript)),
                       LogPanel::Warn);
        publish(DeviceBusy, tr("修复链路"));
        runAction(ActLinkStart);
        return;
    }
    detectInterfaces();
    applyLogLevel();        // 链路就绪后再下发，daemon 不在时执行 wifi -D 必失败
    runAction(m_mode == ModeAp ? ActEnterFSta : ActEnterSta);
}

// 重启 WiFi：stop → start → enterMode。
//
// 为什么不直接 `S41wifi restart`：脚本 stop 有 sleep 1、start 有 sleep 1
// 加最多 3s 的 wlan0 等待，最坏约 6s，超过 ProcessRunner 的 5s 上限，
// 整条命令会被中途 kill，留下 daemon 停了却没起来的半截状态。
// 所以拆成独立命令，用 CommandSequence 串起来，每步都在 5s 之内。
//
// 顺序很关键：之前是 stop → start → prepare(补 STA)，但 prepare 只用
// `wifi -o sta`，把「切回所选的模式」这件事漏掉了。现在统一走 enterMode()。
void WifiService::restartLink()
{
    if (m_actionJob > 0) {
        LogPanel::post(tr("上一个 WiFi 操作还没结束，请稍候"), LogPanel::Info);
        return;
    }
    if (!linkScriptExists()) {
        LogPanel::post(tr("桌面环境没有 %1，WiFi 链路由板端开机脚本提供")
                           .arg(QString::fromLatin1(kLinkScript)),
                       LogPanel::Warn);
        publish(DeviceOff, tr("无开机链路脚本"));
        return;
    }

    publish(DeviceBusy, tr("重启 WiFi"));
    LogPanel::post(tr("重启 WiFi：stop → start → 开启所选模式"), LogPanel::Info);

    CommandSequence *seq = new CommandSequence(tr("重启 WiFi"));
    seq->add(new ShellCommand(CommandSpec(resolveCommand(QStringLiteral("sh")))
                                  .arg(QString::fromLatin1(kLinkScript))
                                  .arg(QStringLiteral("stop"))
                                  .timeout(5000)
                                  .describe(tr("停止链路")),
                              tr("停止 wifi_daemon")))
            ->add(new ShellCommand(CommandSpec(resolveCommand(QStringLiteral("sh")))
                                   .arg(QString::fromLatin1(kLinkScript))
                                   .arg(QStringLiteral("start"))
                                   .timeout(5000)
                                   .describe(tr("启动链路")),
                                   tr("启动 wifi_daemon")));

    // 第三步按所选模式：STA 用 -o sta 开启；AP 用 -f sta 让位（AP 由「开启」按钮单独开）
    if (m_mode == ModeAp) {
        seq->add(new ShellCommand(CommandSpec(resolveCommand(QStringLiteral("wifi")))
                                  .arg(QStringLiteral("-f")).arg(QStringLiteral("sta"))
                                  .timeout(5000),
                                  tr("关闭 STA 让位给 AP")));
    } else {
        seq->add(new ShellCommand(CommandSpec(resolveCommand(QStringLiteral("wifi")))
                                  .arg(QStringLiteral("-o")).arg(QStringLiteral("sta"))
                                  .timeout(5000),
                                  tr("开启 STA 模式")));
    }

    // 重启完要重新取一次状态；用 finished 信号而不是在这里直接 refresh
    connect(seq, &CommandSequence::finished, this,
            [this](bool ok, const QString &summary) {
                if (!ok) {
                    publish(DeviceError, tr("重启失败：%1").arg(summary));
                    return;
                }
                m_staInited = (m_mode == ModeSta);
                LogPanel::post(tr("WiFi 已重启"), LogPanel::Ok);
                detectInterfaces();
                refresh();
                publish(linkReady() ? DeviceReady : DeviceOff,
                        linkReady() ? tr("链路就绪") : tr("链路未就绪"));
            });

    CommandInvoker::instance()->enqueue(seq);
}

void WifiService::runAction(Action action)
{
    m_action = action;
    ProcessRunner *runner = ProcessRunner::instance();

    switch (action) {
    case ActLinkStart:
        m_actionJob = runner->run(resolveCommand(QStringLiteral("sh")),
                                  QStringList() << QString::fromLatin1(kLinkScript)
                                                << QStringLiteral("start"),
                                  5000);
        return;
    case ActEnterSta:
        publish(DeviceBusy, tr("开启 STA"));
        m_actionJob = runner->run(resolveCommand(QStringLiteral("wifi")),
                                  QStringList() << QStringLiteral("-o") << QStringLiteral("sta"),
                                  5000);
        return;
    case ActEnterFSta:
        publish(DeviceBusy, tr("切换到 AP"));
        m_actionJob = runner->run(resolveCommand(QStringLiteral("wifi")),
                                  QStringList() << QStringLiteral("-f") << QStringLiteral("sta"),
                                  5000);
        return;
    case ActOpenAp:
        publish(DeviceBusy, tr("开启热点"));
        m_actionJob = runner->run(resolveCommand(QStringLiteral("wifi")),
                                  QStringList() << QStringLiteral("-o") << QStringLiteral("ap")
                                                << m_apSsid << m_apPassword
                                                << QStringLiteral("6") << m_apSecurity,
                                  5000);
        return;
    case ActCloseAp:
        publish(DeviceBusy, tr("关闭热点"));
        m_actionJob = runner->run(resolveCommand(QStringLiteral("wifi")),
                                  QStringList() << QStringLiteral("-f") << QStringLiteral("ap"),
                                  5000);
        return;
    case ActListAp:
        m_actionJob = runner->run(resolveCommand(QStringLiteral("wifi")),
                                  QStringList() << QStringLiteral("-l") << QStringLiteral("ap"),
                                  5000);
        return;
    case ActConnect:
        publish(DeviceBusy, tr("连接 %1").arg(m_connectSsid));
        m_actionJob = runner->run(resolveCommand(QStringLiteral("wifi")),
                                  QStringList() << QStringLiteral("-c")
                                                << m_connectSsid << m_connectPassword,
                                  5000);
        return;
    case ActDisconnect:
        publish(DeviceBusy, tr("断开连接"));
        m_actionJob = runner->run(resolveCommand(QStringLiteral("wifi")),
                                  QStringList() << QStringLiteral("-d"),
                                  5000);
        return;
    default:
        m_actionJob = -1;
        return;
    }
}

void WifiService::finishAction(Action action, bool ok, const QString &out)
{
    switch (action) {
    case ActLinkStart:
        detectInterfaces();
        if (!linkReady()) {
            publish(DeviceError, tr("链路仍不可用：%1，wifi_daemon 未运行").arg(m_wlan));
            return;
        }
        LogPanel::post(tr("链路已就绪：%1 + wifi_daemon").arg(m_wlan), LogPanel::Ok);
        applyLogLevel();
        runAction(m_mode == ModeAp ? ActEnterFSta : ActEnterSta);
        return;

    case ActEnterSta:
        if (!ok) {
            m_pendingScanAfterSta = false;
            publish(DeviceError, tr("开启 STA 失败"));
            emit scanRefused(tr("开启 STA 失败"));
            return;
        }
        m_staInited = true;
        refresh();
        publish(DeviceBusy, tr("STA 模式就绪"));
        // 之前因为「还没切 STA」被拒绝的扫描，现在补上
        if (m_pendingScanAfterSta) {
            m_pendingScanAfterSta = false;
            scan();
        }
        return;

    case ActEnterFSta:
        // 只是让位，热点还没开；状态由 openAp 结束后更新
        if (!ok)
            LogPanel::post(tr("关闭 STA 失败：%1").arg(out.trimmed()), LogPanel::Warn);
        m_staInited = false;
        publish(DeviceBusy, tr("AP 模式就绪，可开启热点"));
        return;

    case ActOpenAp:
        emit apOpened(ok, ok ? m_apSsid : out.trimmed());
        if (ok) {
            publish(DeviceReady, tr("热点 %1 已开启").arg(m_apSsid));
        } else {
            publish(DeviceError, tr("开启热点失败"));
        }
        return;

    case ActCloseAp:
        emit apOpened(false, QString());
        publish(DeviceOff, ok ? tr("热点已关闭") : tr("关闭热点失败"));
        return;

    case ActListAp: {
        // 输出里既有 AP 自身配置（ssid/ip/channel），也有已连接设备的 MAC。
        //
        // 只以 deviceN 行为准，绝不拿 sta_num 推算数量：实测连两台设备时
        // sta_num 仍然是 1（daemon 那个计数并不等于已连接 STA 数），
        // 拿它当条数会少报。同理也不要拿它做「有没有设备」的判断。
        const QStringList macs = parseApClients(out);
        m_apClientMacs = macs;
        m_apIp = parseApIp(out);
        if (ok) {
            // MAC 来自 wifi -l ap，IP 从 ARP 表反查（同一台设备的两种信息）。
            // 反查不到就留空，页面显示 "--"：设备刚连上时 ARP 还没条目，
            // 下一次点「连接列表」就有了。
            const QHash<QString, QString> arp = arpTable(m_wlan);
            QList<ApClient> clients;
            clients.reserve(macs.size());
            for (int i = 0; i < macs.size(); ++i) {
                ApClient c;
                c.mac = macs.at(i);
                c.ip = arp.value(macs.at(i));
                clients.append(c);
            }
            emit apClients(clients);
            if (clients.isEmpty()) {
                publish(DeviceReady, tr("热点 %1 · 无设备连接").arg(m_apSsid));
            } else {
                publish(DeviceReady, tr("热点 %1 · %2 台设备").arg(m_apSsid).arg(clients.size()));
            }
        } else {
            emit apClientsFailed(out.trimmed().isEmpty() ? tr("读取连接列表失败")
                                                         : out.trimmed());
        }
        return;
    }

    case ActDisconnect:
        refresh();
        if (ok)
            publish(DeviceOff, tr("已断开"));
        else
            publish(DeviceError, tr("断开失败"));
        return;

    case ActConnect:
        if (ok) {
            publish(DeviceReady, tr("已连接 %1").arg(m_connectSsid));
            refresh();
        } else {
            publish(DeviceError, tr("连接 %1 失败").arg(m_connectSsid));
        }
        return;

    default:
        return;
    }
}

void WifiService::openAp(const QString &ssid, const QString &password,
                         int channel, const QString &security)
{
    Q_UNUSED(channel);
    Q_UNUSED(security);
    if (m_actionJob > 0) {
        LogPanel::post(tr("上一个 WiFi 操作还没结束，请稍候"), LogPanel::Info);
        return;
    }
    m_apSsid = ssid;
    m_apPassword = password;
    runAction(ActOpenAp);
}

void WifiService::listApClients()
{
    if (m_actionJob > 0) {
        LogPanel::post(tr("上一个 WiFi 操作还没结束，请稍候"), LogPanel::Info);
        return;
    }
    runAction(ActListAp);
}

void WifiService::closeAp()
{
    if (m_actionJob > 0) {
        LogPanel::post(tr("上一个 WiFi 操作还没结束，请稍候"), LogPanel::Info);
        return;
    }
    runAction(ActCloseAp);
}

// ── 扫描 ────────────────────────────────────────────────────────
// 流程：wpa_cli scan（触发） → 倒计时 N 秒（默认 5，可配） → wpa_cli scan_results
//
// 倒计时不是装饰：这块板整轮扫描约 1.7s，立刻查 scan_results 会读到上一轮
// 的旧列表。用倒计时既等够了时间，也把「还要等多久」告诉用户。
void WifiService::scan()
{
    if (m_scanJob > 0) {
        LogPanel::post(tr("扫描已在进行中"), LogPanel::Info);
        emit scanRefused(tr("扫描已在进行中"));
        return;
    }
    if (m_mode == ModeAp) {
        LogPanel::post(tr("AP 模式下不扫描上游热点"), LogPanel::Warn);
        emit scanRefused(tr("AP 模式下不扫描"));
        return;
    }
    if (!linkReady()) {
        LogPanel::post(tr("链路未就绪，先修复链路"), LogPanel::Info);
        emit scanRefused(tr("链路未就绪"));
        enterMode();
        return;
    }
    // STA 模式是扫描的前提，但 m_staInited 只是本进程的内存标记：
    // 进程重启后它是 false，而板端可能早就是 STA 了；enterMode 走
    // ActLinkStart/ActEnterFSta 分支时也不会置它。所以要区分两件事——
    //   · 已经切过 STA（m_staInited）→ 直接扫
    //   · 没切过 → 先补一次 wifi -o sta，成功后再扫
    // 之所以不无条件先切：wifi -o sta 幂等但要走一次驱动 open/close，
    // 每次点扫描都做一遍没有必要，还会打断可能正在进行的关联。
    if (!m_staInited) {
        LogPanel::post(tr("先切换到 STA 模式再扫描（m_staInited=false）"), LogPanel::Info);
        m_pendingScanAfterSta = true;
        emit scanRefused(tr("正在切换到 STA…"));
        enterMode();
        return;
    }

    ++m_scanSeq;                    // 让上一轮的回调作废
    m_scanStep = 0;
    m_scanLeft = scanSeconds();     // 倒计时同时是「持续轮询」的总时长
    publish(DeviceBusy, tr("扫描中"));
    LogPanel::post(tr("开始扫描：持续 %1 秒，每秒刷新一次结果").arg(m_scanLeft),
                   LogPanel::Debug);
    submitScanStep(0);
}

void WifiService::stopScan()
{
    if (m_scanJob < 0 && m_scanLeft <= 0)
        return;
    ++m_scanSeq;                    // 作废所有待触发的回调
    m_scanTimer->stop();
    m_scanLeft = 0;
    if (m_scanJob > 0) {
        ProcessRunner::instance()->cancel(m_scanJob);
        m_scanJob = -1;
    }
    emit scanCountdown(-1);
    LogPanel::post(tr("扫描已停止"), LogPanel::Info);
}

// 扫描期间的每秒 tick。
//
// 语义是「持续取结果并刷新列表」，不是「等 N 秒只取一次」：
// 每一拍都去拉一次 scan_results，拿到就 emit，列表随扫随长。
// 这样用户点完扫描很快就能看到已扫到的热点，而不是盯着空白等满 5 秒。
//
// m_scanJob 是「当前有没有一条 scan_results 在飞」的标记：
// 上一拍还没回来就跳过这一拍，避免命令堆积。
void WifiService::onScanTick()
{
    if (m_scanLeft <= 0) {
        m_scanTimer->stop();
        return;
    }

    --m_scanLeft;
    emit scanCountdown(m_scanLeft);

    // 到点了：本拍照常拉一次（作为最后一拍），之后停表
    const bool last = (m_scanLeft <= 0);
    if (last) {
        m_scanTimer->stop();
        m_scanLeft = 0;
    }

    // 上一拍的结果还没回来就跳过这一拍，不堆积命令
    if (m_scanJob >= 0)
        return;

    m_scanStep = 1;
    m_scanJob = ProcessRunner::instance()->run(
        resolveCommand(QStringLiteral("wpa_cli")),
        QStringList() << QStringLiteral("-p") << wpaCtrlDir()
                      << QStringLiteral("-i") << m_wlan
                      << QStringLiteral("scan_results"),
        5000);
}

void WifiService::submitScanStep(int step)
{
    m_scanStep = step;
    ProcessRunner *runner = ProcessRunner::instance();
    const QString ctrl = wpaCtrlDir();

    switch (step) {
    case 0:
        // 绝对路径：板端 wpa_cli 在 /usr/sbin，而 init 拉起的进程
        // PATH 不一定含它（见 resolveCommand 的注释）
        m_scanJob = runner->run(resolveCommand(QStringLiteral("wpa_cli")),
                                QStringList() << QStringLiteral("-p") << ctrl
                                              << QStringLiteral("-i") << m_wlan
                                              << QStringLiteral("scan"),
                                5000);
        return;
    case 1:
        m_scanJob = runner->run(resolveCommand(QStringLiteral("wpa_cli")),
                                QStringList() << QStringLiteral("-p") << ctrl
                                              << QStringLiteral("-i") << m_wlan
                                              << QStringLiteral("scan_results"),
                                5000);
        return;
    case 2:
        m_scanJob = runner->run(resolveCommand(QStringLiteral("iw")),
                                QStringList() << QStringLiteral("dev") << m_wlan
                                              << QStringLiteral("scan"),
                                5000);
        return;
    default:
        // 理论上走不到（step 只会有 0/1/2）。但必须有收尾：
        // 没有 scanCountdown(-1) 页面不会恢复按钮，没有 scanResult
        // 列表不会刷新，两个信号缺一个就会卡住。
        LogPanel::post(tr("扫描失败：无可用数据源"), LogPanel::Error);
        emit scanCountdown(-1);
        emit scanResult(QString());
        publish(DeviceError, tr("无法扫描"));
        return;
    }
}

void WifiService::connectTo(const QString &ssid, const QString &password)
{
    if (ssid.trimmed().isEmpty()) {
        LogPanel::post(tr("SSID 为空，取消连接"), LogPanel::Warn);
        return;
    }
    if (!linkReady()) {
        LogPanel::post(tr("链路未就绪，先开启 STA"), LogPanel::Warn);
        enterMode();
        return;
    }
    m_connectSsid = ssid;
    m_connectPassword = password;
    runAction(ActConnect);
}

void WifiService::disconnect()
{
    runAction(ActDisconnect);
}

void WifiService::applyLogLevel()
{
    if (!linkReady())
        return;                         // daemon 不在时执行 wifi -D 只会失败
    AppSettings *settings = AppSettings::instance();
    if (!settings)
        return;
    const QString level = settings->wifiLogLevel();
    if (level.isEmpty() || level == m_appliedLogLevel)
        return;                         // 幂等：同一个等级不重复下发

    m_appliedLogLevel = level;
    LogPanel::post(tr("设置 WiFi 驱动日志等级：%1").arg(level), LogPanel::Info);
    // 用 quiet：某些版本对不认识的等级会打 usage 并返回非零，
    // 那是配置问题，不该在日志里刷成一条 Error。
    ProcessRunner::instance()->submit(CommandSpec(resolveCommand(QStringLiteral("wifi")))
                                          .arg(QStringLiteral("-D"))
                                          .arg(level)
                                          .timeout(5000)
                                          .quiet()
                                          .describe(tr("设置 WiFi 日志等级")));
}

void WifiService::refresh()
{
    if (!hasWirelessNode()) {
        detectInterfaces();
        if (!hasWirelessNode()) {
            publish(DeviceOff, tr("未检测到无线网卡"));
            return;
        }
    }
    m_statusJob = ProcessRunner::instance()->run(resolveCommand(QStringLiteral("ip")),
                                                 QStringList() << QStringLiteral("-4")
                                                               << QStringLiteral("addr")
                                                               << QStringLiteral("show") << m_wlan,
                                                 5000);
}

CommandSequence *WifiService::makeConnectSequence(const QString &ssid, const QString &password)
{
    CommandSequence *sequence = new CommandSequence(tr("WiFi 一键联网：%1").arg(ssid));
    sequence->add(new ShellCommand(CommandSpec(resolveCommand(QStringLiteral("sh")))
                                       .arg(QString::fromLatin1(kLinkScript))
                                       .arg(QStringLiteral("start"))
                                       .timeout(5000)
                                       .describe(tr("确保链路")),
                                   tr("确保链路")))
            ->add(new ShellCommand(CommandSpec(resolveCommand(QStringLiteral("wifi")))
                                   .arg(QStringLiteral("-o")).arg(QStringLiteral("sta"))
                                   .timeout(5000),
                                   tr("切换 STA 模式")))
            ->add(new ShellCommand(CommandSpec(resolveCommand(QStringLiteral("wifi")))
                                   .arg(QStringLiteral("-c")).arg(ssid).arg(password).timeout(5000),
                                   tr("连接 %1").arg(ssid)))
            ->add(new ShellCommand(CommandSpec(resolveCommand(QStringLiteral("ip")))
                                   .arg(QStringLiteral("-4")).arg(QStringLiteral("addr"))
                                   .arg(QStringLiteral("show")).arg(m_wlan).timeout(5000),
                                   tr("读取 IP")));
    return sequence;
}

bool WifiService::scanOutputUsable(const QString &out) const
{
    if (out.trimmed().isEmpty())
        return false;
    static const QRegularExpression rx(
        QStringLiteral("(^|\\n)\\s*(BSS\\s+[0-9a-fA-F:]{17}|[0-9a-fA-F:]{17}\\s+\\d+)"));
    return rx.match(out).hasMatch();
}

void WifiService::onFinished(int jobId, int exitCode, int, const QString &out)
{
    if (jobId == m_scanJob) {
        m_scanJob = -1;

        if (m_scanStep == 0) {
            const bool accepted = (exitCode == 0)
                                  && !out.contains(QStringLiteral("FAIL"), Qt::CaseInsensitive);
            if (accepted) {
                // 触发成功：立刻先拉一次，然后开始每秒轮询。
                // 不等第一秒——多数情况下 scan 返回时 supplicant 已扫完，
                // 能立刻看到一批结果，少等一秒。
                // m_scanLeft 已在 scan() 里设好，这里不要再覆盖。
                emit scanCountdown(m_scanLeft);
                m_scanStep = 1;
                m_scanJob = ProcessRunner::instance()->run(
                    resolveCommand(QStringLiteral("wpa_cli")),
                    QStringList() << QStringLiteral("-p") << wpaCtrlDir()
                                  << QStringLiteral("-i") << m_wlan
                                  << QStringLiteral("scan_results"),
                    5000);
                m_scanTimer->start();
                return;
            }
            LogPanel::post(tr("wpa_cli scan 未受理，改试 iw"), LogPanel::Warn);
            m_scanStep = 2;
            submitScanStep(2);
            return;
        }

        // ── 轮询拍：step 1 是 scan_results，step 2 是 iw 兜底 ──
        //
        // 轮询模式下每一拍都只是一次「刷新」，不能因为某一拍没扫到就
        // 换数据源 —— 扫描本来就要时间，前几拍空是正常的。
        // 只有整段时间跑完（m_scanLeft 已归零）才算一轮结束。
        if (exitCode == 0 && scanOutputUsable(out)) {
            emit scanResult(out);
            // iw 是一次性拿全量，拿到就结束；scan_results 的轮询由
            // 定时器控制节奏，这里只负责刷新列表。
            if (m_scanStep == 2) {
                m_scanTimer->stop();
                m_scanLeft = 0;
                emit scanCountdown(0);
            }
            return;
        }

        // 这一拍没结果，或命令失败。记录但继续等下一拍，
        // 直到 5 秒跑完由页面的 scanCountdown(0) 收尾。
        if (exitCode != 0) {
            LogPanel::post(tr("scan_results 失败（exit=%1），等待下一拍")
                               .arg(exitCode),
                           LogPanel::Debug);
        } else if (m_scanStep == 2) {
            // iw 成功但没 BSS：确实没扫到，结束
            m_scanTimer->stop();
            m_scanLeft = 0;
            emit scanResult(QString());
            emit scanCountdown(0);
        }
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
        const Action action = static_cast<Action>(m_action);
        m_action = ActNone;
        finishAction(action, exitCode == 0, out);
        return;
    }
}

void WifiService::onFailed(int jobId, const QString &reason)
{
    if (jobId == m_scanJob) {
        m_scanJob = -1;

        if (m_scanStep == 0) {
            // 第 0 步（wpa_cli scan）失败说明 supplicant 控制 socket 就没通，
            // 再查 scan_results 也必然失败。整个扫描到此结束：
            // 停表 + 换 iw 也不行的话就没有别的数据源了。
            m_scanTimer->stop();
            m_scanLeft = 0;
            LogPanel::post(tr("wpa_cli 不可用（%1），回退 iw").arg(reason), LogPanel::Warn);
            // iw 是一次性拿全量结果，走完就结束，不再轮询
            m_scanStep = 2;
            submitScanStep(2);
            return;
        }

        if (m_scanStep == 2) {
            // iw 兜底也失败：没有任何数据源可用，必须收尾
            m_scanTimer->stop();
            m_scanLeft = 0;
            emit scanCountdown(-1);
            LogPanel::post(tr("扫描失败：%1").arg(reason), LogPanel::Error);
            emit scanResult(QString());
            publish(DeviceError, tr("扫描失败"));
            return;
        }

        // step 1 是轮询中的某一拍失败：不中断，等下一拍重试。
        // 扫描本来就要时间，中途某一拍失败（supplicant 忙）是正常的。
        LogPanel::post(tr("scan_results 失败（%1），等待下一拍").arg(reason),
                       LogPanel::Debug);
        return;
    }
    if (jobId == m_statusJob) {
        m_statusJob = -1;
        publish(DeviceOff, reason);
        return;
    }
    if (jobId == m_actionJob) {
        m_actionJob = -1;
        const Action action = static_cast<Action>(m_action);
        m_action = ActNone;
        finishAction(action, false, reason);
        return;
    }
}

HMI_REGISTER_SERVICE("wifi", WifiService)
