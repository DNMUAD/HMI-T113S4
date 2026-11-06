#ifndef I2CPAGE_H
#define I2CPAGE_H

#include "basepage.h"

class QLabel;
class QSpinBox;

// M3：/dev/i2c-2，0x03–0x77 只读扫描（默认不写，避免打扰触摸 0x5d）
class I2cPage : public BasePage
{
    Q_OBJECT
public:
    explicit I2cPage(QWidget *parent = nullptr);

protected:
    void buildContent() override;

private:
    QLabel   *m_result;
    QSpinBox *m_address;
    QSpinBox *m_register;
};

#endif // I2CPAGE_H
