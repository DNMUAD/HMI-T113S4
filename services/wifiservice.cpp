#include "wifiservice.h"
#include "command.h"
#include "logpanel.h"
#include "processrunner.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTimer>

namespace {

// 开机链路脚本：装载驱动 → ifconfig up → 起 wifi_daemon（幂等）
const char kLinkScript[] = "/etc/init.d/S41wifi";

// wpa_cli scan 之后等多久再取结果。
// 实测这块板整轮扫描约 1670ms（驱动日志：41 个信道），留点余量取 2000ms；
// 取太小会读到上一轮的旧列表，取太大只是让用户多等一点。
const int kScanSettleMs = 2000;

// wpa_supplicant 控制 socket 目录。这块板是 /etc/wifi/wpa_supplicant/sockets，
// 常规发行版是 /var/run/wpa_supplicant 或 /run/wpa_supplicant —— 按顺序取第一个
// 真实存在且里面有 <iface> socket 的，避免换镜像就得改代码。
QString wpaCtrlDir()
{
    const QStringList candidates = QStringList()
            << QStringLiteral("/etc/wifi/wpa_supplicant/sockets")
            << QStringLiteral("/var/run/wpa_supplicant")
            << QStringLiteral("/run/wpa_supplicant");
    for (int i = 0; i < candidates.size(); ++i) {
        QDir d(candidates.at(i));
        if (d.exists())
            return d.absolutePath();     // absolutePath() 会去掉末尾 '/'
    }
    // 都不存在时仍返回板端约定路径，让 wpa_cli 自己的报错更有指向性
    return candidates.first();
}

QString firstInet(const QString &out)
{
    QRegularExpression rx(QStringLiteral("\\binet\\s+(\\d+\\.\\d+\\.\\d+\\.\\d+)"));
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

// 开机脚本是否存在。桌面版没有它，相关操作要给出可读的提示而不是
// 一条 "sh: can't open" 的错误。
bool linkScriptExists()
{
    return QFile::exists(QString::fromLatin1(kLinkScript));
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
    , m_scanStep(0)
    , m_scanSeq(0)
    , m_preparing(false)
    , m_restarting(false)
    , m_prepareStep(0)
    , m_fixStep(0)
    , m_staInited(false)
    , m_pendingAction(PendingNone)
    , m_wlan(QStringLiteral("wlan0"))
{
    connect(ProcessRunner::instance(), &ProcessRunner::finished,
            this, &WifiService::onFinished);
    connect(ProcessRunner::instance(), &ProcessRunner::failed,
            this, &WifiService::onFailed);
    detectInterfaces();
}

// 桌面机器通常没有无线网卡，直接按 wlan0 探测会每 5s 刷一条命令失败。
// 这里先看 /sys/class/net，再决定用哪个名字。
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
    // 找 phy80211 子目录（任何无线接口都有）
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

// 确保链路就绪 → 切 STA。
// 链路由 /etc/init.d/S41wifi 负责；这里只在检测到缺失时重跑一次脚本，
// 不再自己 insmod / 起 daemon（重复实现只会和开机脚本抢设备）。
void WifiService::prepare()
{
    if (m_preparing || m_restarting)
        return;
    m_preparing = true;
    m_prepareStep = 0;
    publish(DeviceBusy, tr("检查 WiFi 链路"));
    stepPrepareNext();
}

// 强制重启开机链路：stop → start → prepare（补 STA）。
// daemon 活着但 socket 坏了时，start 是幂等的会直接跳过，只有 restart 才治得了。
//
// 为什么不直接 `S41wifi restart`：脚本里 stop 有 sleep 1、start 有 sleep 1
// 加最多 3s 的 wlan0 等待，最坏约 6s，超过 ProcessRunner 硬性 5s 钳制，
// 整条命令会被中途 kill，留下 daemon 停了却没起来的半截状态。
// 所以拆成两次提交，每次都在 5s 之内，用 m_fixStep 串起来。
void WifiService::restartLink()
{
    if (m_preparing || m_restarting)
        return;
    if (!linkScriptExists()) {
        LogPanel::post(tr("桌面环境没有 %1，WiFi 链路由板端开机脚本提供")
                           .arg(QString::fromLatin1(kLinkScript)),
                       LogPanel::Warn);
        publish(DeviceOff, tr("无开机链路脚本"));
        return;
    }
    m_restarting = true;
    m_fixStep = 1;
    publish(DeviceBusy, tr("重启 WiFi 链路"));
    LogPanel::post(tr("重启链路：先 stop（清理残留 socket 与 daemon）"), LogPanel::Info);
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("sh"),
                                                  QStringList() << QString::fromLatin1(kLinkScript)
                                                                << QStringLiteral("stop"),
                                                  5000);
}

// restartLink 的第二、第三段：stop 结束后接着 start，start 结束后转向 prepare。
// failReason 为空表示上一条命令成功；非空表示它失败了。
// 返回 true 表示这次结果已被链路修复流程消费。
bool WifiService::continueLinkFix(const QString &failReason)
{
    if (m_fixStep == 1) {
        // stop 失败不致命（可能本来就没在跑），照样往下走
        if (!failReason.isEmpty())
            LogPanel::post(tr("stop 未成功（%1），继续尝试 start").arg(failReason),
                           LogPanel::Warn);
        m_fixStep = 2;
        LogPanel::post(tr("重启链路：再 start"), LogPanel::Info);
        m_actionJob = ProcessRunner::instance()->run(QStringLiteral("sh"),
                                                      QStringList() << QString::fromLatin1(kLinkScript)
                                                                    << QStringLiteral("start"),
                                                      5000);
        return true;
    }
    if (m_fixStep == 2) {
        m_fixStep = 0;
        m_restarting = false;
        detectInterfaces();
        if (!failReason.isEmpty() || !linkReady()) {
            const QString why = failReason.isEmpty()
                                    ? tr("%1=%2，wifi_daemon=%3")
                                          .arg(m_wlan,
                                               hasWirelessNode() ? tr("有") : tr("无"),
                                               daemonReady() ? tr("运行中") : tr("未运行"))
                                    : failReason;
            failPrepare(DeviceError, tr("重启后链路仍不可用：%1").arg(why));
            return true;
        }
        LogPanel::post(tr("链路已重启：%1 + wifi_daemon").arg(m_wlan), LogPanel::Ok);
        // 脚本不负责 STA 模式，交给 prepare 补上（此时链路已就绪，会直接跳到 STA）
        prepare();
        return true;
    }
    return false;
}

// 准备流程失败的统一收尾。集中在一处，是为了保证「挂起的连接意图」一定被清掉——
// 否则用户在准备失败后点一次「检查链路」，会莫名其妙地自动连上一次的 SSID。
void WifiService::failPrepare(DeviceState state, const QString &detail)
{
    const bool hadPending = (m_pendingAction != PendingNone);
    m_pendingAction = PendingNone;
    m_pendingSsid.clear();
    m_pendingPassword.clear();
    m_preparing = false;
    m_prepareStep = 0;

    publish(state, detail);
    emit prepared(false);
    if (hadPending)
        LogPanel::post(tr("链路准备失败，已取消挂起的扫描/连接"), LogPanel::Warn);
}

void WifiService::stepPrepareNext()
{
    ProcessRunner *runner = ProcessRunner::instance();

    switch (m_prepareStep) {
    case 0:
        m_prepareStep = 1;
        if (linkReady()) {
            LogPanel::post(tr("%1 与 wifi_daemon 均已就绪（开机脚本已拉起），跳过修复")
                               .arg(m_wlan),
                           LogPanel::Info);
            stepPrepareNext();
            return;
        }
        if (!linkScriptExists()) {
            failPrepare(DeviceOff, hasWirelessNode()
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
        publish(DeviceBusy, tr("修复开机链路"));
        m_actionJob = runner->run(QStringLiteral("sh"),
                                  QStringList() << QString::fromLatin1(kLinkScript)
                                                << QStringLiteral("start"),
                                  5000);
        return;

    case 1:
        m_prepareStep = 2;
        // 脚本刚跑完，重新探测一次名字（驱动可能是刚装上才出现的）
        detectInterfaces();
        if (!linkReady()) {
            failPrepare(DeviceError, tr("链路仍不可用：%1=%2，wifi_daemon=%3")
                                         .arg(m_wlan,
                                              hasWirelessNode() ? tr("有") : tr("无"),
                                              daemonReady() ? tr("运行中") : tr("未运行")));
            return;
        }
        LogPanel::post(tr("链路已就绪：%1 + wifi_daemon").arg(m_wlan), LogPanel::Ok);
        stepPrepareNext();
        return;

    case 2:
        m_prepareStep = 3;
        LogPanel::post(QStringLiteral("切换 Station 模式"), LogPanel::Info);
        m_actionJob = runner->run(QStringLiteral("wifi"),
                                  QStringList() << QStringLiteral("-o") << QStringLiteral("sta"),
                                  5000);
        return;

    default:
        m_preparing = false;
        m_staInited = true;      // 走完 case 2 就一定切过 STA 了
        refresh();
        emit prepared(true);
        // 若这次 prepare 是被「扫描」或「连接」触发的，就绪后把动作做完
        runPendingAction();
        return;
    }
}

void WifiService::initSta()
{
    if (!linkReady()) {
        LogPanel::post(tr("链路未就绪（需要 %1 与 wifi_daemon），先准备链路").arg(m_wlan),
                       LogPanel::Warn);
        prepare();
        return;
    }
    publish(DeviceBusy, tr("初始化 STA 模式"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                  QStringList() << QStringLiteral("-o") << QStringLiteral("sta"),
                                                  5000);
}

void WifiService::initAp()
{
    if (!linkReady()) {
        LogPanel::post(tr("链路未就绪，先准备链路"), LogPanel::Warn);
        prepare();
        return;
    }
    publish(DeviceBusy, tr("切换 AP 模式"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                  QStringList() << QStringLiteral("-o") << QStringLiteral("ap"),
                                                  5000);
}

void WifiService::scan()
{
    if (m_preparing || m_restarting) {
        LogPanel::post(tr("上一个链路操作还没结束，稍后再扫描"), LogPanel::Info);
        return;
    }
    // 扫描的前提是 STA 模式。开机脚本只到 daemon，若本进程还没切过 STA，
    // 先走一遍 prepare（它会补齐 STA）再扫，否则 wifi -s 会报未初始化。
    if (!linkReady() || !m_staInited) {
        m_pendingAction = PendingScan;
        prepare();
        return;
    }
    doScan();
}

void WifiService::connectTo(const QString &ssid, const QString &password)
{
    if (ssid.trimmed().isEmpty()) {
        LogPanel::post(QStringLiteral("SSID 为空，取消连接"), LogPanel::Warn);
        return;
    }

    // 记住这次要连谁。连接一律走 prepare()：它会确认链路、再切 STA
    // （开机脚本不做 STA），就绪后自动接着连，用户只需点一次。
    m_pendingSsid = ssid;
    m_pendingPassword = password;

    if (m_preparing || m_restarting) {
        m_pendingAction = PendingConnect;
        LogPanel::post(tr("上一个链路操作还没结束，%1 将在其完成后连接").arg(ssid),
                       LogPanel::Info);
        return;
    }
    m_pendingAction = PendingConnect;
    prepare();
}

// 连接动作本体。切 STA 是必须的：开机脚本只到 daemon 为止，
// 没切 STA 就直接 wifi -c，接口还停在未初始化状态，注册流程会失败。
void WifiService::runPendingAction()
{
    const int action = m_pendingAction;
    m_pendingAction = PendingNone;

    switch (action) {
    case PendingConnect:
        doConnect();
        return;
    case PendingScan:
        doScan();
        return;
    default:
        publish(DeviceBusy, tr("WiFi 链路就绪"));
        return;
    }
}

// 连接动作本体。调用前 prepare() 已确保链路就绪并切到 STA 模式。
void WifiService::doConnect()
{
    publish(DeviceBusy, tr("连接 %1").arg(m_pendingSsid));
    // wifi -c 内部会调 udhcpc 取 IP，连上后 ifconfig 就能看到地址
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                  QStringList() << QStringLiteral("-c")
                                                                << m_pendingSsid << m_pendingPassword,
                                                  5000);
    refresh();
}

// 扫描动作本体。依赖 prepare() 已切好 STA。
//
// 为什么不用 `wifi -s`：这个客户端只把命令写进 /tmp/UNIX_WIFI.domain 就退出，
// 二进制里连 read/recv 符号都没有（只有 socket/connect/write），扫描结果由
// wifi_daemon 用它自己的 stdout 打印（也就是板端 debug 窗口那些 WINF 行）。
// 因此 `wifi -s` 的退出码是 0 但 stdout 永远为空，QProcess 抓不到任何东西。
//
// 改用 wpa_cli：它连 wpa_supplicant 的控制 socket，把结果 printf 到 stdout，
// QProcess 能正常捕获；wifi_daemon 内部用的也是同一个 supplicant，数据一致。
// 注意两个命令是分开的：scan 触发，scan_results 取结果，中间要等扫描完成。
void WifiService::doScan()
{
    m_scanStep = 0;
    ++m_scanSeq;      // 让上一轮还没触发的等待回调作废
    publish(DeviceBusy, tr("扫描中"));
    submitScanStep();
}

// 扫描数据源按顺序试：
//   0  wpa_cli -p <ctrl_dir> -i wlan0 scan         （触发扫描，本身不输出列表）
//   1  wpa_cli -p <ctrl_dir> -i wlan0 scan_results （真正取结果的命令）
//   2  iw dev wlan0 scan                           （没有 wpa_supplicant 时兜底）
//
// 为什么 wpa_cli 优先：wifi_daemon 内部就是用 wpa_supplicant 管理接口的，
// wpa_cli 连的是同一个控制 socket，拿到的就是 daemon 已经扫好的结果；
// 而 iw 走 nl80211 直连，在 daemon 占着接口时容易 EBUSY 或扫不全。
// 只有 ctrl socket 不存在（没有 wpa_supplicant）时才退回 iw。
void WifiService::submitScanStep()
{
    ProcessRunner *runner = ProcessRunner::instance();
    const QString ctrl = wpaCtrlDir();

    switch (m_scanStep) {
    case 0:
        // -p 指定 socket 目录是本板的关键：默认路径 /var/run/wpa_supplicant
        // 在这块板上不存在，不传 -p 会直接 "Failed to connect to wpa_supplicant"
        m_scanJob = runner->run(QStringLiteral("wpa_cli"),
                                 QStringList() << QStringLiteral("-p") << ctrl
                                               << QStringLiteral("-i") << m_wlan
                                               << QStringLiteral("scan"),
                                 5000);
        return;
    case 1:
        // scan 只是触发；列表必须再查一次 scan_results
        m_scanJob = runner->run(QStringLiteral("wpa_cli"),
                                 QStringList() << QStringLiteral("-p") << ctrl
                                               << QStringLiteral("-i") << m_wlan
                                               << QStringLiteral("scan_results"),
                                 5000);
        return;
    case 2:
        LogPanel::post(tr("wpa_cli（%1）不可用，回退 iw dev %2 scan").arg(ctrl, m_wlan),
                       LogPanel::Warn);
        m_scanJob = runner->run(QStringLiteral("iw"),
                                 QStringList() << QStringLiteral("dev") << m_wlan
                                               << QStringLiteral("scan"),
                                 5000);
        return;
    default:
        LogPanel::post(tr("wpa_cli（%1）与 iw 都不可用，请检查 wpa_supplicant 与驱动状态")
                           .arg(ctrl),
                       LogPanel::Error);
        publish(DeviceError, tr("无法扫描"));
        return;
    }
}

void WifiService::disconnect()
{
    publish(DeviceBusy, tr("断开连接"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("wifi"),
                                                  QStringList() << QStringLiteral("-d"),
                                                  5000);
    refresh();
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
    m_statusJob = ProcessRunner::instance()->run(QStringLiteral("ip"),
                                                  QStringList() << QStringLiteral("-4") << QStringLiteral("addr")
                                                                << QStringLiteral("show") << m_wlan,
                                                  5000);
}

CommandSequence *WifiService::makeConnectSequence(const QString &ssid, const QString &password)
{
    // 第 1 步交给开机脚本（幂等：链路已就绪时它什么都不做，缺什么补什么），
    // 之后才是 STA → 连接 → 取 IP。
    CommandSequence *sequence = new CommandSequence(tr("WiFi 一键联网：%1").arg(ssid));
    sequence->add(new ShellCommand(CommandSpec(QStringLiteral("sh"))
                                       .arg(QString::fromLatin1(kLinkScript))
                                       .arg(QStringLiteral("start"))
                                       .timeout(5000)
                                       .describe(tr("确保开机链路")),
                                   tr("确保链路（S41wifi start）")))
            ->add(new ShellCommand(CommandSpec(QStringLiteral("wifi"))
                                   .arg(QStringLiteral("-o")).arg(QStringLiteral("sta"))
                                   .timeout(5000),
                                   tr("切换 STA 模式")))
            ->add(new ShellCommand(CommandSpec(QStringLiteral("wifi"))
                                   .arg(QStringLiteral("-c")).arg(ssid).arg(password).timeout(5000),
                                   tr("连接 %1").arg(ssid)))
            ->add(new ShellCommand(CommandSpec(QStringLiteral("ip"))
                                   .arg(QStringLiteral("-4")).arg(QStringLiteral("addr"))
                                   .arg(QStringLiteral("show")).arg(m_wlan).timeout(5000),
                                   tr("读取 IP")));
    return sequence;
}

// 扫描输出是否真的含有可用结果。
// 光看退出码不够：iw 在驱动没扫到任何 AP 时也会返回 0 且输出为空/只有表头，
// 那种情况要继续回退到下一个数据源，而不是把空列表当成扫描成功。
bool WifiService::scanOutputUsable(const QString &out) const
{
    if (out.trimmed().isEmpty())
        return false;
    // iw: "BSS aa:bb:..."；wpa_cli: "aa:bb:cc:dd:ee:ff\t<freq>\t..."
    static const QRegularExpression rx(
        QStringLiteral("(^|\\n)\\s*(BSS\\s+[0-9a-fA-F:]{17}|[0-9a-fA-F:]{17}\\s+\\d+)"));
    return rx.match(out).hasMatch();
}

void WifiService::onFinished(int jobId, int exitCode, int, const QString &out)
{
    if (jobId == m_scanJob) {
        m_scanJob = -1;

        // 第 0 步是 wpa_cli scan（只触发扫描，输出是 "OK"）。它成功不代表结果已就绪，
        // 实测这块板整轮扫描约 1.7s，立刻查 scan_results 会读到上一轮的旧列表。
        // 所以先等一会儿再取结果——用定时器，不阻塞 UI。
        if (m_scanStep == 0) {
            const bool accepted = (exitCode == 0)
                                  && !out.contains(QStringLiteral("FAIL"), Qt::CaseInsensitive);
            if (accepted) {
                LogPanel::post(tr("扫描已触发，等待结果"), LogPanel::Info);
                m_scanStep = 1;
                // 捕获本轮次：这段时间里用户若又点了一次扫描，m_scanSeq 会变，
                // 旧回调直接作废，避免两个扫描流程交错把结果写乱。
                const int seq = m_scanSeq;
                QTimer::singleShot(kScanSettleMs, this, [this, seq]() {
                    if (seq == m_scanSeq && m_scanStep == 1 && m_scanJob < 0)
                        submitScanStep();
                });
                return;
            }
            LogPanel::post(tr("wpa_cli scan 未受理（%1），改试 iw").arg(out.trimmed()),
                           LogPanel::Warn);
            m_scanStep = 2;
            submitScanStep();
            return;
        }

        // 第 1 步 scan_results / 第 2 步 iw scan：直接看输出里有没有 BSS。
        if (exitCode == 0 && scanOutputUsable(out)) {
            m_scanStep = 0;
            emit scanResult(out);
            refresh();
            return;
        }

        // scan_results / iw scan 成功但没有 BSS，通常是「确实没扫到」而不是工具坏了，
        // 这时不该再换数据源白等一轮，直接如实报「没有热点」。
        // 只有 exit!=0 或输出含失败字样（socket 不通、权限不足）才继续下一源。
        const bool failedHard = (exitCode != 0)
                                || out.contains(QStringLiteral("Failed"), Qt::CaseInsensitive)
                                || out.contains(QStringLiteral("Could not"), Qt::CaseInsensitive);
        if (!failedHard && m_scanStep >= 1) {
            m_scanStep = 0;
            LogPanel::post(tr("扫描完成，但周围没有可用热点"), LogPanel::Warn);
            emit scanResult(out);
            refresh();
            return;
        }

        ++m_scanStep;
        submitScanStep();
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

        // 链路修复流程（restartLink）优先消费。
        // 注意 executed 非零也要传成失败，否则 stop 失败会被当成成功。
        if (continueLinkFix(exitCode == 0 ? QString()
                                          : tr("exit=%1").arg(exitCode)))
            return;

        // 准备流程走到中途：按步骤继续
        if (m_preparing) {
            if (exitCode != 0) {
                failPrepare(DeviceError,
                            tr("准备失败（第 %1 步，exit=%2）").arg(m_prepareStep).arg(exitCode));
                return;
            }
            stepPrepareNext();
            return;
        }

        refresh();
    }
}

void WifiService::onFailed(int jobId, const QString &reason)
{
    if (jobId == m_scanJob) {
        // 当前数据源失败（程序不存在、无权限、socket 连不上等），换下一个。
        // 第 0 步（wpa_cli scan）失败说明 supplicant 控制 socket 就没通，
        // 再查 scan_results 也必然失败，直接跳到 iw 兜底。
        m_scanJob = -1;
        if (m_scanStep == 0) {
            LogPanel::post(tr("wpa_cli 不可用（%1），回退 iw").arg(reason), LogPanel::Warn);
            m_scanStep = 2;
        } else {
            ++m_scanStep;
        }
        submitScanStep();
        return;
    }
    if (jobId == m_statusJob) {
        m_statusJob = -1;
        publish(DeviceOff, reason);
        return;
    }
    if (jobId == m_actionJob) {
        m_actionJob = -1;
        // 失败也交给修复流程判断（stop 失败可继续，start 失败则收尾报错）
        if (continueLinkFix(reason))
            return;
        if (m_preparing) {
            failPrepare(DeviceError, tr("准备失败（第 %1 步）").arg(m_prepareStep));
            return;
        }
        publish(DeviceError, reason);
    }
}

// 自注册：注册表据此做「全部刷新」与周期探测
HMI_REGISTER_SERVICE("wifi", WifiService)
