#ifndef WIFIPAGE_H
#define WIFIPAGE_H

#include <QModelIndex>

#include "basepage.h"

class QLineEdit;
class QListView;
class QPlainTextEdit;
class WifiScanModel;

// M2：AIC8800D80，wifi -o sta / -s / -c / -d，ip -4 addr show wlan0
//
// 页面只做三件事：搭界面、把用户动作转成服务调用、订阅状态总线显示结果。
class WifiPage : public BasePage
{
    Q_OBJECT
public:
    explicit WifiPage(QWidget *parent = nullptr);
    void onEnter() override;

protected:
    void buildContent() override;

private slots:
    void onScanResult(const QString &raw);
    void onAccessPointActivated(const QModelIndex &index);
    void onLinkChanged();
    void onSequenceFinished(const QString &description, bool ok, const QString &summary);

private:
    QLineEdit      *m_ssid;
    QLineEdit      *m_password;
    QListView      *m_scanView;
    QPlainTextEdit *m_rawView;
    WifiScanModel  *m_model;
    bool            m_waitingSequence;
};

#endif // WIFIPAGE_H
