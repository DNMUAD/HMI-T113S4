#ifndef ADBPAGE_H
#define ADBPAGE_H

#include "basepage.h"

// M2：/etc/init.d/S50adb_start start|stop，pidof adbd + /sys/class/udc
class AdbPage : public BasePage
{
    Q_OBJECT
public:
    explicit AdbPage(QWidget *parent = nullptr);
    void onEnter() override;

protected:
    void buildContent() override;
};

#endif // ADBPAGE_H
