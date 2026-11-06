#include "ethpage.h"
#include "commandinvoker.h"
#include "ethservice.h"
#include "pagefactory.h"
#include "statusbus.h"

#include <QGridLayout>
#include <QLabel>

EthPage::EthPage(QWidget *parent)
    : BasePage(tr("以太网"), parent)
    , m_ip(nullptr)
    , m_link(nullptr)
    , m_waitingSequence(false)
{
}

void EthPage::buildContent()
{
    // 硬约定：界面常驻提示
    setHint(tr("跳线帽：左 RS485 / 右 以太网。切到以太网前请确认右侧跳线帽已装。"));

    m_ip = new QLabel(tr("IP: -"), this);
    m_link = new QLabel(tr("链路: -"), this);
    m_ip->setObjectName(QStringLiteral("pageStatus"));
    m_link->setObjectName(QStringLiteral("pageStatus"));
    addWidget(m_ip);
    addWidget(m_link);

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("刷新"), []() { EthService::instance()->refresh(); },
                              tr("读取 IP / operstate / speed"), true), 0, 0);
    grid->addWidget(newButton(tr("网卡 UP"), []() { EthService::instance()->bringUp(); },
                              tr("ifconfig eth0 up")), 0, 1);
    grid->addWidget(newButton(tr("DHCP"), []() { EthService::instance()->dhcp(); },
                              tr("udhcpc -i eth0")), 1, 0);
    grid->addWidget(newButton(tr("Ping 测试"), []() { EthService::instance()->pingTest(); },
                              tr("ping -c4 www.baidu.com")), 1, 1);
    grid->addWidget(newButton(tr("一键联网"), [this]() {
        m_waitingSequence = true;
        CommandInvoker::instance()->enqueue(EthService::instance()->makeConnectSequence());
    }, tr("网卡 UP → DHCP → Ping 验证（任一步失败即中断）"), true), 2, 0);

    connect(EthService::instance(), &EthService::pingResult, this, [this](const QString &raw) {
        setStatus(raw.split(QLatin1Char('\n')).last());
    });
    connect(EthService::instance(), &EthService::linkInfoChanged, this,
            [this](const QString &state, const QString &speed) {
        m_link->setText(tr("链路: %1 / %2").arg(state, speed));
    });
    connect(StatusBus::instance(), &StatusBus::changed, this, [this](const DeviceStatus &status) {
        if (status.device == QStringLiteral("eth"))
            refreshLabels();
    });
    connect(CommandInvoker::instance(), &CommandInvoker::sequenceFinished,
            this, &EthPage::onSequenceFinished);
}

void EthPage::onEnter()
{
    EthService::instance()->refresh();
    refreshLabels();
}

void EthPage::refreshLabels()
{
    const DeviceStatus status = StatusBus::instance()->status(QStringLiteral("eth"));
    m_ip->setText(tr("IP: %1").arg(status.detail.isEmpty() ? tr("未获取") : status.detail));
    setStatus(tr("%1 · %2").arg(deviceStateName(status.state), status.detail));
}

void EthPage::onSequenceFinished(const QString &description, bool ok, const QString &summary)
{
    if (!m_waitingSequence)
        return;
    m_waitingSequence = false;
    setStatus(ok ? tr("「%1」完成 · %2").arg(description, summary)
                 : tr("「%1」失败 · %2").arg(description, summary));
    EthService::instance()->refresh();
}

HMI_REGISTER_PAGE("eth", 80, QT_TR_NOOP("以太网"), EthPage)
