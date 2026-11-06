#ifndef HOMEVIEW_H
#define HOMEVIEW_H

#include "baseview.h"

// 首页：空壳工程里用来验证框架链路
class HomeView : public BaseView
{
    Q_OBJECT
public:
    explicit HomeView(QWidget *parent = nullptr);

    QString title() const override;

signals:
    // 请求切换到指定页面，由 MainWindow 接管
    void requestPage(int index);
};

#endif // HOMEVIEW_H
