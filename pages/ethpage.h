#ifndef ETHPAGE_H
#define ETHPAGE_H

#include "basepage.h"

class QLabel;

// M2：ifconfig eth0 up → udhcpc -i eth0 → ping -c4 www.baidu.com
// 「一键联网」走 CommandSequence（组合命令），比连点三个按钮可靠得多。
class EthPage : public BasePage
{
    Q_OBJECT
public:
    explicit EthPage(QWidget *parent = nullptr);
    void onEnter() override;

protected:
    void buildContent() override;

private slots:
    void onSequenceFinished(const QString &description, bool ok, const QString &summary);

private:
    void refreshLabels();

    QLabel *m_ip;
    QLabel *m_link;
    bool    m_waitingSequence;
};

#endif // ETHPAGE_H
