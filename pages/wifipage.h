#ifndef WIFIPAGE_H
#define WIFIPAGE_H

#include <QList>
#include <QStringList>

#include "basepage.h"
#include "wifiservice.h"      // ApClient（AP 设备条目：MAC + IP）

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QWidget;
class WifiScanModel;

// WiFi 页：STA / AP 两种模式，一屏内切换。
//
// 布局（800x480，必须不出现纵向滚动条）：
//   标题 / 提示 / 状态行
//   [SSID] [密码]                       ← 输入行
//   [切换模式] [重启WiFi] [扫描] [连接]  ← 按钮行（按模式变化）
//   [列表 / 连接设备列表]                ← 与按钮区互斥占位
//
// 按钮按模式切换：
//   STA：扫描 / 连接 / 断开
//   AP ：开启 / 连接列表 / 关闭
// 公共按钮：切换模式、重启 WiFi。
class WifiPage : public BasePage
{
    Q_OBJECT
public:
    explicit WifiPage(QWidget *parent = nullptr);
    void onEnter() override;
    void onLeave() override;

protected:
    void buildContent() override;

private slots:
    void onModeChanged();
    void onScanCountdown(int seconds);
    void onScanRefused(const QString &reason);
    void onScanResult(const QString &raw);
    void onApClients(const QList<ApClient> &clients);
    void onApClientsFailed(const QString &reason);
    void onItemActivated(const QModelIndex &index);
    void onLinkChanged();
    void toggleMode();
    void doScan();
    void doConnect();
    void doDisconnect();

private:
    void buildInputRow(QWidget *parent);
    void buildButtonRow(QWidget *parent);
    void applyMode();
    void setListMode(bool apClients);
    void showApClients(const QStringList &lines);

    QLineEdit      *m_ssid;
    QLineEdit      *m_password;
    QListView      *m_scanView;
    QListView      *m_apView;
    QStackedWidget *m_listStack;
    WifiScanModel  *m_model;

    QPushButton    *m_modeBtn;
    QPushButton    *m_restartBtn;
    QPushButton    *m_scanBtn;
    QPushButton    *m_connectBtn;
    QPushButton    *m_disconnectBtn;
    QPushButton    *m_apOpenBtn;
    QPushButton    *m_apListBtn;
    QPushButton    *m_apCloseBtn;

    QStringList     m_apLines;
};

#endif // WIFIPAGE_H
