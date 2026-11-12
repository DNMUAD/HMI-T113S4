#ifndef WEBSHAREPAGE_H
#define WEBSHAREPAGE_H

#include "basepage.h"

class QLabel;
class QTimer;
class CardButton;
class QrCodeWidget;

// 局域网网页文件管理页（扫码打开）
//
// 页面只做展示与开关：地址与二维码来自 WebShareService，文件操作在其后端的 HTTP 服务里，
// 页面从不直接碰 QTcpSocket 或 QFile。
class WebSharePage : public BasePage
{
    Q_OBJECT
public:
    explicit WebSharePage(QWidget *parent = nullptr);
    void onEnter() override;
    void onLeave() override;

protected:
    void buildContent() override;

private slots:
    void refreshUi();
    void toggleServer();

private:
    QrCodeWidget *m_qr;
    QLabel       *m_url;
    QLabel       *m_info;
    QLabel       *m_roots;
    CardButton   *m_toggle;
    QTimer       *m_timer;
};

#endif // WEBSHAREPAGE_H
