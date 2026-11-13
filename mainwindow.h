#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QString>

#include "devicestatus.h"

class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QLabel;
class QTimer;
class QPushButton;
class BasePage;
class CardButton;
class LogPanel;
class SoftKeyboard;
class StatusDot;

// 左导航(QListWidget 168px，可收起成 56px 窄条) + 内容区 QStackedWidget
// + 统一 LogPanel + 状态栏(时间 / WiFi / ETH / ADB)
//
// 窗口不再认识任何具体页面或服务：
//  · 页面来自 PageFactory 注册表（工厂 + 自注册）；
//  · 状态来自 StatusBus 广播（观察者），本类只把 device 映射成状态灯。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

public slots:
    void setCurrentPage(int stackIndex);   // 0=总览，1..9=9 个功能页

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateClock();
    void onNavItemClicked(QListWidgetItem *item);
    void onStatusChanged(const DeviceStatus &status);
    void toggleNav();

private:
    void buildUi();
    void buildPages();
    void buildStatusBar();
    void subscribeStatus();
    void buildSoftKeyboard();
    void setNavCollapsed(bool collapsed);
    void applyNavForm();
    void applyCompactMetrics();

    QWidget        *m_navPanel;
    QListWidget    *m_nav;
    QPushButton    *m_navToggle;
    QStackedWidget *m_stack;
    CardButton     *m_homeButton;
    LogPanel       *m_log;
    SoftKeyboard   *m_keyboard;
    QWidget        *m_central;
    QLabel         *m_clock;
    QTimer         *m_clockTimer;
    QHash<QString, StatusDot *> m_dots;    // device id → 状态灯
    QList<BasePage *> m_pages;
    QList<int>        m_navPages;          // 导航行 → 页面索引（总览不占导航）
    bool            m_navCollapsed;
    bool            m_navUserOverride;     // 用户手动切换过，就不再按宽度自动收起
    bool            m_compact;             // 480 高屏幕：日志让位给功能区
    int             m_current;
};

#endif // MAINWINDOW_H
