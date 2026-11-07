#include "canpage.h"
#include "pagefactory.h"
#include "processrunner.h"
#include "logpanel.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>

#ifdef HMI_HAVE_CAN
#include <QCanBus>
#endif

CanPage::CanPage(QWidget *parent)
    : BasePage(tr("CAN 总线"), parent)
    , m_iface(nullptr)
    , m_bitrate(nullptr)
    , m_frameType(nullptr)
    , m_period(nullptr)
    , m_status(nullptr)
{
}

void CanPage::buildContent()
{
    setHint(tr("M5 前置条件：内核 CONFIG_CAN=y / CAN_RAW / CAN_DEV / AWLINK_SUN8I=y 后 bash ./build.sh kernel；"
               " Qt 侧需 qmake CONFIG+=hmi_can（QT += serialbus）。DTS 的 awlink0/awlink1 节点已 status=\"okay\"。"));

    m_iface = new QComboBox(this);
    m_iface->setEditable(true);
    m_iface->addItems(QStringList() << QStringLiteral("awlink0") << QStringLiteral("awlink1"));
    addWidget(m_iface);

    m_bitrate = new QComboBox(this);
    m_bitrate->addItems(QStringList() << QStringLiteral("125000") << QStringLiteral("250000")
                                      << QStringLiteral("500000") << QStringLiteral("1000000"));
    addWidget(m_bitrate);

    m_frameType = new QComboBox(this);
    m_frameType->addItems(QStringList() << tr("标准帧") << tr("扩展帧"));
    addWidget(m_frameType);

    m_period = new QSpinBox(this);
    m_period->setRange(0, 5000);
    m_period->setSuffix(tr(" ms"));
    m_period->setValue(1000);
    addWidget(m_period);

    m_status = new QLabel(tr("未检测"), this);
    addWidget(m_status);

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("检测设备"), []() {
        ProcessRunner::instance()->run(QStringLiteral("ip"),
                                       QStringList() << QStringLiteral("-d") << QStringLiteral("link")
                                                     << QStringLiteral("show"));
    }, tr("ip -d link show，期望 type can"), true), 0, 0);
    grid->addWidget(newButton(tr("配置位速率"), [this]() {
        ProcessRunner::instance()->run(QStringLiteral("ip"),
                                       QStringList() << QStringLiteral("link") << QStringLiteral("set")
                                                     << m_iface->currentText() << QStringLiteral("type")
                                                     << QStringLiteral("can") << QStringLiteral("bitrate")
                                                     << m_bitrate->currentText());
    }, tr("ip link set ... type can bitrate")), 0, 1);
    grid->addWidget(newButton(tr("启用接口"), [this]() {
        ProcessRunner::instance()->run(QStringLiteral("ip"),
                                       QStringList() << QStringLiteral("link") << QStringLiteral("set")
                                                     << m_iface->currentText() << QStringLiteral("up"));
    }, tr("ip link set awlink0 up")), 1, 0);
    grid->addWidget(newButton(tr("回环模式"), [this]() {
        ProcessRunner::instance()->run(QStringLiteral("ip"),
                                       QStringList() << QStringLiteral("link") << QStringLiteral("set")
                                                     << m_iface->currentText() << QStringLiteral("type")
                                                     << QStringLiteral("can") << QStringLiteral("loopback")
                                                     << QStringLiteral("on"));
    }, tr("无对端时自发自收")), 1, 1);
    addButton(tr("发送测试帧"), [this]() {
        const QString frame = m_frameType->currentIndex() == 0
                              ? QStringLiteral("123#1122334455667788")
                              : QStringLiteral("12345678#1122334455667788");
        ProcessRunner::instance()->run(QStringLiteral("cansend"),
                                       QStringList() << m_iface->currentText() << frame);
    }, tr("cansend awlink0 123#1122334455667788"));

    connect(ProcessRunner::instance(), &ProcessRunner::finished, this,
            [this](int, int code, int, const QString &out) {
        if (out.contains(QStringLiteral("type can")))
            m_status->setText(tr("已检测到 CAN 接口（type can）"));
        else if (code != 0)
            m_status->setText(tr("命令执行失败，详见日志"));
    });

#ifdef HMI_HAVE_CAN
    // M5：QCanBus::instance()->plugins() 找 socketcan →
    //     createDevice("socketcan", "awlink0") → connectDevice() → writeFrame()/framesReceived()
    LogPanel::post(tr("CAN 插件: %1").arg(QCanBus::instance()->plugins().join(QStringLiteral(", "))), LogPanel::Info);
#endif
}

HMI_REGISTER_PAGE("can", 60, QT_TR_NOOP("CAN"), CanPage)
