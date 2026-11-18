#ifndef BTPAGE_H
#define BTPAGE_H

#include <QList>
#include <QString>

#include "basepage.h"
#include "btdevice.h"

class QLabel;
class QListView;
class QPushButton;
class QStackedWidget;
class QTimer;
class BtScanModel;

// 蓝牙页。
//
// 布局沿用 WiFi 页验证过的做法：扫描列表与配对面板用 QStackedWidget
// **互斥占用同一块区域**，整体高度不变。
// 原因（wifipage.cpp 里有完整记录）：800x480 扣掉状态栏/日志/标题/提示/
// 状态行/按钮行之后内容区只剩一百多像素，上下堆叠必然把页面顶出滚动条，
// 一滚按钮就被带走，用户会以为按钮消失了。互斥显示永远不用滚。
//
// 不弹 QDialog：板端 QPA 只有 liblinuxfb.so，是单窗口平台，
// 第二个顶层窗口在实机上很可能根本不显示（软键盘因此也做成子控件贴底）。
// 配对确认同样走 QStackedWidget 切页。
class BtPage : public BasePage
{
    Q_OBJECT
public:
    explicit BtPage(QWidget *parent = nullptr);
    void onEnter() override;
    void onLeave() override;

protected:
    void buildContent() override;

private slots:
    void onAdapterStateChanged(bool on);
    void onScanStateChanged(int state);
    void onDeviceFound(const BtDeviceRecord &device);
    void onDeviceRemoved(const QString &address);
    void onRssiUpdated(const QString &address, int rssi);
    void onBondStateChanged(int state, const QString &address);
    void onPasskeyRequested(quintptr handle, const QString &address,
                            unsigned int passkey, bool numericOnly);
    void onPairedListChanged(const QList<BtDeviceRecord> &devices);
    void onPairTimeout();
    void onDeviceActivated(const QModelIndex &index);

private:
    QWidget *buildActionRow();
    QWidget *buildScanPanel();
    QWidget *buildConfirmPanel();

    void refreshButtons();
    void showConfirm(bool visible);
    void resolvePendingPasskey(bool accept);
    void setEmptyHint(const QString &text);

    QListView      *m_scanView;
    BtScanModel    *m_scanModel;
    QStackedWidget *m_stack;
    QLabel         *m_emptyHint;
    QLabel         *m_confirmText;
    QPushButton    *m_powerButton;
    QPushButton    *m_scanButton;
    QPushButton    *m_confirmButton;
    QPushButton    *m_rejectButton;
    QTimer         *m_pairTimer;
    quintptr        m_pendingHandle;
    QString         m_pendingAddress;
    bool            m_adapterOn;
    bool            m_scanning;
};

#endif // BTPAGE_H
