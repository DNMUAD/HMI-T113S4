#ifndef BASEVIEW_H
#define BASEVIEW_H

#include <QWidget>

// 页面基类：所有页面继承它，由 MainWindow 统一装载、切换
class BaseView : public QWidget
{
    Q_OBJECT
public:
    explicit BaseView(QWidget *parent = nullptr);

    // 页面标题，显示在标题栏
    virtual QString title() const = 0;

    // 进入 / 离开页面时的钩子，默认空实现
    virtual void onEnter() {}
    virtual void onLeave() {}
};

#endif // BASEVIEW_H
