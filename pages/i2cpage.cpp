#include "i2cpage.h"
#include "i2cservice.h"
#include "pagefactory.h"

#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>

I2cPage::I2cPage(QWidget *parent)
    : BasePage(tr("I2C 总线"), parent)
    , m_result(nullptr)
    , m_address(nullptr)
    , m_register(nullptr)
{
}

void I2cPage::buildContent()
{
    setHint(tr("/dev/i2c-2 · 只读扫描 0x03–0x77（I2C_RDWR + I2C_M_RD），默认不写寄存器，避免打扰触摸 0x5d。"));

    m_result = new QLabel(tr("尚未扫描"), this);
    m_result->setWordWrap(true);
    m_result->setObjectName(QStringLiteral("pageStatus"));
    addWidget(m_result);

    m_address = new QSpinBox(this);
    m_address->setRange(0x03, 0x77);
    m_address->setDisplayIntegerBase(16);
    m_address->setPrefix(QStringLiteral("0x"));
    m_address->setValue(0x5d);
    addWidget(m_address);

    m_register = new QSpinBox(this);
    m_register->setRange(0x00, 0xFF);
    m_register->setDisplayIntegerBase(16);
    m_register->setPrefix(QStringLiteral("0x"));
    addWidget(m_register);

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("扫描设备"), []() { I2cService::instance()->scan(); }, tr("期望扫出 0x5d"), true), 0, 0);
    grid->addWidget(newButton(tr("读寄存器"), [this]() {
        I2cService::instance()->readRegister(m_address->value(), m_register->value());
    }, tr("TODO(M3)")), 0, 1);

    connect(I2cService::instance(), &I2cService::scanFinished, this,
            [this](const QVector<int> &addresses) {
        QStringList hex;
        for (int a : addresses)
            hex << QStringLiteral("0x%1").arg(a, 2, 16, QLatin1Char('0'));
        m_result->setText(hex.isEmpty() ? tr("未发现应答设备") : tr("发现设备: %1").arg(hex.join(QStringLiteral("  "))));
    });
}

HMI_REGISTER_PAGE("i2c", 40, QT_TR_NOOP("I2C"), I2cPage)
