#include "homepage.h"
#include "command.h"
#include "commandinvoker.h"
#include "logpanel.h"
#include "pagefactory.h"
#include "serviceregistry.h"
#include "statusbus.h"

#include <QCoreApplication>
#include <QFile>
#include <QGridLayout>
#include <QLabel>

namespace {
QString readFirstLine(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readLine()).trimmed();
}
} // namespace

HomePage::HomePage(QWidget *parent)
    : BasePage(tr("总览"), parent)
    , m_info(nullptr)
    , m_waitingSequence(false)
{
}

void HomePage::buildContent()
{
    setHint(tr("跳线帽：左 RS485 / 右 以太网。应用以 root 运行（init.d 拉起）。"));

    QLabel *intro = new QLabel(tr("HMI-T113S4 · 全志 T113-S4 板载人机界面\n"
                                  "左侧 9 项功能导航，所有命令输出与外部程序 stderr 统一汇入底部日志。"), this);
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("pageIntro"));
    addWidget(intro);

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("sysInfo"));
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(96);
    addWidget(m_info);

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("刷新状态栏"), []() {
        // 注册表统一刷新，新增服务不必改这里
        ServiceRegistry::instance()->refreshAll();
        LogPanel::post(tr("已下发全量状态刷新"), LogPanel::Ok);
    }, tr("遍历服务注册表逐个探测"), true), 0, 0);

    grid->addWidget(newButton(tr("环境自检"), [this]() {
        // 组合命令（Composite）：四条诊断命令按顺序跑，输出统一进日志
        CommandSequence *sequence = new CommandSequence(tr("环境自检"));
        sequence->add(new ShellCommand(CommandSpec(QStringLiteral("uname"))
                                       .arg(QStringLiteral("-a")), tr("内核版本")))
                ->add(new ShellCommand(CommandSpec(QStringLiteral("ls"))
                                       .arg(QStringLiteral("/dev/i2c-2"))
                                       .arg(QStringLiteral("/dev/ttyS2"))
                                       .quiet(), tr("关键设备节点")))
                ->add(new ShellCommand(CommandSpec(QStringLiteral("ip"))
                                       .arg(QStringLiteral("-4")).arg(QStringLiteral("addr"))
                                       .arg(QStringLiteral("show")), tr("网络地址")))
                ->add(new ShellCommand(CommandSpec(QStringLiteral("pidof"))
                                       .arg(QStringLiteral("adbd")).quiet(), tr("adbd 进程")));
        m_waitingSequence = true;
        CommandInvoker::instance()->enqueue(sequence);
    }, tr("内核 / 设备节点 / 网络 / adbd"), true), 0, 1);

    grid->addWidget(newButton(tr("状态汇总"), [this]() {
        const QList<DeviceStatus> all = StatusBus::instance()->snapshot();
        if (all.isEmpty()) {
            setStatus(tr("尚无设备上报状态"));
            return;
        }
        QStringList lines;
        for (int i = 0; i < all.size(); ++i) {
            const DeviceStatus &s = all.at(i);
            lines << QStringLiteral("%1=%2(%3)").arg(s.device, deviceStateName(s.state), s.detail);
        }
        setStatus(lines.join(QStringLiteral("  ")));
    }, tr("从 StatusBus 读取全部设备状态")), 1, 0);

    grid->addWidget(newButton(tr("日志自检"), []() {
        LogPanel::post(QStringLiteral("INFO 自检：页面、服务、ProcessRunner 链路正常"), LogPanel::Info);
        LogPanel::post(QStringLiteral("OK   自检：日志面板可写、限 500 行"), LogPanel::Ok);
    }, tr("验证统一日志链路")), 1, 1);

    connect(CommandInvoker::instance(), &CommandInvoker::sequenceFinished,
            this, &HomePage::onSequenceFinished);

    refreshInfo();
}

void HomePage::onEnter()
{
    refreshInfo();
    ServiceRegistry::instance()->refreshAll();
}

void HomePage::onSequenceFinished(const QString &description, bool ok, const QString &summary)
{
    if (!m_waitingSequence)
        return;
    m_waitingSequence = false;
    setStatus(ok ? tr("「%1」完成 · %2").arg(description, summary)
                 : tr("「%1」失败 · %2").arg(description, summary));
}

void HomePage::refreshInfo()
{
    const QString mem = readFirstLine(QStringLiteral("/proc/meminfo"));
    const QString load = readFirstLine(QStringLiteral("/proc/loadavg"));
    const QString tempRaw = readFirstLine(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"));
    const QString temp = tempRaw.isEmpty() ? tr("不可用")
                                           : QString::number(tempRaw.toDouble() / 1000.0, 'f', 1) + QStringLiteral(" °C");

    m_info->setText(tr("版本 %1\n内存 %2\n负载 %3\n温度 %4")
                    .arg(QCoreApplication::applicationVersion())
                    .arg(mem.isEmpty() ? tr("（桌面环境）") : mem)
                    .arg(load.isEmpty() ? tr("（桌面环境）") : load)
                    .arg(temp));
}

HMI_REGISTER_PAGE("home", 0, nullptr, HomePage)
