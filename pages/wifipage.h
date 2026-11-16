#ifndef WIFIPAGE_H
#define WIFIPAGE_H

#include <QModelIndex>

#include "basepage.h"

class QGridLayout;
class QLabel;
class QLineEdit;
class QListView;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QWidget;
class WifiScanModel;

// AIC8800D80。链路（驱动 + wlan0 + wifi_daemon）由 /etc/init.d/S41wifi 开机拉起，
// 本页只做三件事：搭界面、把用户动作转成服务调用、订阅状态总线显示结果。
//
// 界面按「常用 / 诊断」两级摆放（800x480 的屏，8 个按钮平铺根本放不下）：
//   常用：扫描 / 连接 / 断开
//   诊断：「更多」展开后才出现 —— 检查链路 / 重启链路 / 刷新 IP / 一键联网 / AP 模式
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
    void toggleAdvanced();

private:
    QWidget *buildCredentialRow();
    QWidget *buildAdvancedPanel();

    QLineEdit      *m_ssid;
    QLineEdit      *m_password;
    QListView      *m_scanView;
    QPlainTextEdit *m_rawView;
    QStackedWidget *m_scanStack;      // 0=扫描列表 1=诊断面板（互斥同一块区域）
    QWidget        *m_advancedPanel;
    QPushButton    *m_advancedBtn;
    WifiScanModel  *m_model;
    bool            m_waitingSequence;
};

#endif // WIFIPAGE_H
