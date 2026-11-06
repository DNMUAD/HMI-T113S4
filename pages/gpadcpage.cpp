#include "gpadcpage.h"
#include "gpadcservice.h"
#include "pagefactory.h"

#include <QGridLayout>
#include <QLabel>

GpadcPage::GpadcPage(QWidget *parent)
    : BasePage(tr("GPADC 按键"), parent)
    , m_device(nullptr)
    , m_lastKey(nullptr)
{
}

void GpadcPage::buildContent()
{
    setHint(tr("内核 CONFIG_SUNXI_GPADC=y 已开，无需改内核。键码映射：115 音量+ / 114 音量- / 119 暂停 / 373 播放 / 28 确认。"));

    m_device = new QLabel(tr("设备: 未查找"), this);
    m_lastKey = new QLabel(tr("最近按键: -"), this);
    m_device->setObjectName(QStringLiteral("pageStatus"));
    m_lastKey->setObjectName(QStringLiteral("pageStatus"));
    addWidget(m_device);
    addWidget(m_lastKey);

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("查找设备"), [this]() {
        const QString node = GpadcService::instance()->findEventNode();
        m_device->setText(node.isEmpty() ? tr("设备: 未找到 gpadc") : tr("设备: %1").arg(node));
    }, tr("扫 /proc/bus/input/devices"), true), 0, 0);
    grid->addWidget(newButton(tr("开始监听"), []() { GpadcService::instance()->start(); }, tr("QSocketNotifier 异步读事件")), 0, 1);
    grid->addWidget(newButton(tr("停止监听"), []() { GpadcService::instance()->stop(); }, tr("释放 eventN")), 1, 0);

    connect(GpadcService::instance(), &GpadcService::keyEvent, this,
            [this](int code, const QString &name, int value) {
        m_lastKey->setText(tr("最近按键: %1 (code=%2, value=%3)").arg(name).arg(code).arg(value));
    });
    connect(GpadcService::instance(), &GpadcService::deviceChanged, this,
            [this](const QString &node) {
        m_device->setText(node.isEmpty() ? tr("设备: 未找到 gpadc") : tr("设备: %1").arg(node));
    });
}

void GpadcPage::onEnter()
{
    if (GpadcService::instance()->isRunning())
        return;
    const QString node = GpadcService::instance()->findEventNode();
    m_device->setText(node.isEmpty() ? tr("设备: 未找到 gpadc") : tr("设备: %1（点「开始监听」启用）").arg(node));
}

HMI_REGISTER_PAGE("gpadc", 70, QT_TR_NOOP("GPADC"), GpadcPage)
