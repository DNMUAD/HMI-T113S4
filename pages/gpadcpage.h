#ifndef GPADCPAGE_H
#define GPADCPAGE_H

#include "basepage.h"

class QLabel;

// M3：CONFIG_SUNXI_GPADC=y 已开；/proc/bus/input/devices 找 gpadc 的 eventN，
// QSocketNotifier 读 struct input_event，映射 115/114/119/373/28。
class GpadcPage : public BasePage
{
    Q_OBJECT
public:
    explicit GpadcPage(QWidget *parent = nullptr);
    void onEnter() override;

protected:
    void buildContent() override;

private:
    QLabel *m_device;
    QLabel *m_lastKey;
};

#endif // GPADCPAGE_H
