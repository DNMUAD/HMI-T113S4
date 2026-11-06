#ifndef SETTINGVIEW_H
#define SETTINGVIEW_H

#include "baseview.h"

// 设置页：演示 qss 生效，以及页面如何承载表单
class SettingView : public BaseView
{
    Q_OBJECT
public:
    explicit SettingView(QWidget *parent = nullptr);

    QString title() const override;
};

#endif // SETTINGVIEW_H
