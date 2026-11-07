#include "rs485page.h"
#include "pagefactory.h"
#include "rs485service.h"
#include "statusbus.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QPlainTextEdit>

Rs485Page::Rs485Page(QWidget *parent)
    : BasePage(tr("RS485"), parent)
    , m_port(nullptr)
    , m_baud(nullptr)
    , m_sendText(nullptr)
    , m_rxView(nullptr)
{
}

void Rs485Page::buildContent()
{
    setHint(tr("跳线帽：左 RS485 / 右 以太网。半双工方向由驱动 sunxi,uart-485oe 管，应用层无需切换。"
               " 板端请先 ls /dev/ttyS* + dmesg | grep ttyS 核对端口（文档写的 /dev/ttyRS485 本 SDK 未验证）。"));

    m_port = new QComboBox(this);
    m_port->setEditable(true);
    m_port->addItems(Rs485Service::availablePorts());
    addWidget(m_port);

    m_baud = new QComboBox(this);
    m_baud->addItems(Rs485Service::baudRates());
    m_baud->setCurrentText(QStringLiteral("115200"));
    addWidget(m_baud);

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("打开串口"), [this]() {
        Rs485Service::instance()->open(m_port->currentText(), m_baud->currentText().toInt());
    }, tr("8N1，无流控"), true), 0, 0);
    grid->addWidget(newButton(tr("关闭串口"), []() { Rs485Service::instance()->close(); }, tr("释放 /dev/ttyS2")), 0, 1);

    m_sendText = new QLineEdit(this);
    m_sendText->setPlaceholderText(tr("发送内容（与 USB-485 对端互测，或 TX/RX 短接回环）"));
    addWidget(m_sendText);
    addButton(tr("发送"), [this]() {
        Rs485Service::instance()->send(m_sendText->text().toUtf8());
    }, tr("写入串口"), false);

    m_rxView = new QPlainTextEdit(this);
    m_rxView->setReadOnly(true);
    m_rxView->setMaximumBlockCount(200);
    m_rxView->setMinimumHeight(100);
    m_rxView->setPlaceholderText(tr("接收区"));
    addWidget(m_rxView);

    connect(Rs485Service::instance(), &Rs485Service::frameReceived, this,
            [this](const QByteArray &data) {
        m_rxView->appendPlainText(QString::fromUtf8(data));
    });
    connect(StatusBus::instance(), &StatusBus::changed, this, [this](const DeviceStatus &status) {
        if (status.device != QStringLiteral("rs485"))
            return;
        setStatus(tr("%1 · %2").arg(deviceStateName(status.state), status.detail));
    });
}

HMI_REGISTER_PAGE("rs485", 50, QT_TR_NOOP("RS485"), Rs485Page)
