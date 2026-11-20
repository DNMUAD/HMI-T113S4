#ifndef GPADCPAGE_H
#define GPADCPAGE_H

#include <QList>

#include "basepage.h"

class QComboBox;
class QGridLayout;
class QLabel;

// GPADC 按键页（板端 T113-S4 的 K1 / K2 / K3）
//
// 页面只做两件事：把设备与事件如实回显，以及让用户把某个键绑到某个动作。
// 「动作」来自 GpadcService::actions()，页面不认识具体动作——以后加音量、
// 播放、切页等动作时，只在那边的模块里 addAction()，这里零改动。
//
// 布局按 800x480 的实际可用高度来：状态行 + 设备行 + 3 行「按键→动作」
// + 两列按钮，不依赖滚动。
class GpadcPage : public BasePage
{
    Q_OBJECT
public:
    explicit GpadcPage(QWidget *parent = nullptr);
    void onEnter() override;

protected:
    void buildContent() override;

private:
    struct Row
    {
        int        code;
        QLabel    *count;
        QComboBox *action;
    };

    void rebuildActions();                 // 动作表变化后重填所有下拉框
    void refreshDevice();
    void refreshCounters();
    void setCounting(bool on);

    QLabel               *m_device;
    QLabel               *m_last;
    QList<Row>            m_rows;
};

#endif // GPADCPAGE_H
