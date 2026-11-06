#include "homeview.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

HomeView::HomeView(QWidget *parent)
    : BaseView(parent)
{
    setObjectName(QStringLiteral("homeView"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(48, 40, 48, 40);
    layout->setSpacing(16);

    QLabel *title = new QLabel(QStringLiteral("软件框架已就绪"), this);
    title->setObjectName(QStringLiteral("pageTitle"));

    QLabel *desc = new QLabel(
        QStringLiteral("业务功能代码已全部移除，这里保留的是可直接扩展的工程骨架：\n"
                       "\n"
                       "  • 页面基类 BaseView：继承后实现 title()，即可被主窗口装载\n"
                       "  • 主窗口 MainWindow：标题栏 / 页面容器 / 状态栏三段式布局\n"
                       "  • 日志 Logger：qDebug 等统一落盘到 logs/app.log\n"
                       "  • 配置 AppConfig：默认读写 config/app.ini\n"
                       "  • 样式 resources/qss/style.qss：沿用原工程的配色与控件样式"),
        this);
    desc->setObjectName(QStringLiteral("pageDesc"));
    desc->setWordWrap(true);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QPushButton *button = new QPushButton(QStringLiteral("进入设置页"), this);
    button->setObjectName(QStringLiteral("primaryButton"));
    button->setFixedSize(200, 44);
    connect(button, &QPushButton::clicked, this, [this]() {
        emit requestPage(1);
    });

    QHBoxLayout *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(button);
    buttonRow->addStretch(1);

    layout->addWidget(title);
    layout->addWidget(desc);
    layout->addSpacing(8);
    layout->addLayout(buttonRow);
    layout->addStretch(1);
}

QString HomeView::title() const
{
    return QStringLiteral("首页");
}
