#ifndef BTPAGE_H
#define BTPAGE_H

#include "basepage.h"

// 蓝牙：M1 仅控制器状态与接口占位，配对/扫描待 M6
class BtPage : public BasePage
{
    Q_OBJECT
public:
    explicit BtPage(QWidget *parent = nullptr);
    void onEnter() override;

protected:
    void buildContent() override;
};

#endif // BTPAGE_H
