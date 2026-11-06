#ifndef CANPAGE_H
#define CANPAGE_H

#include "basepage.h"

class QComboBox;
class QSpinBox;
class QLabel;

// M5：awlink0/awlink1（sunxi-awlink）。M1 用 ip 命令做设备发现与基础配置，
// QCanBus 收发路径在 qmake CONFIG+=hmi_can 下预留。
class CanPage : public BasePage
{
    Q_OBJECT
public:
    explicit CanPage(QWidget *parent = nullptr);

protected:
    void buildContent() override;

private:
    QComboBox *m_iface;
    QComboBox *m_bitrate;
    QComboBox *m_frameType;
    QSpinBox  *m_period;
    QLabel    *m_status;
};

#endif // CANPAGE_H
