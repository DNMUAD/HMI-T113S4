#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include "basepage.h"

class QLabel;

// 总览页：系统信息 + 状态自检入口（导航顶部独立按钮进入，不占 9 项功能导航）
class HomePage : public BasePage
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);
    void onEnter() override;

protected:
    void buildContent() override;

private slots:
    void onSequenceFinished(const QString &description, bool ok, const QString &summary);

private:
    void refreshInfo();
    QLabel *m_info;
    bool    m_waitingSequence;
};

#endif // HOMEPAGE_H
