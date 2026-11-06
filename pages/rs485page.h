#ifndef RS485PAGE_H
#define RS485PAGE_H

#include "basepage.h"

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

// M3：QSerialPort("/dev/ttyS2")，半双工方向由驱动 sunxi,uart-485oe 管，应用层不切 DE/RE
class Rs485Page : public BasePage
{
    Q_OBJECT
public:
    explicit Rs485Page(QWidget *parent = nullptr);

protected:
    void buildContent() override;

private:
    QComboBox      *m_port;
    QComboBox      *m_baud;
    QLineEdit      *m_sendText;
    QPlainTextEdit *m_rxView;
};

#endif // RS485PAGE_H
