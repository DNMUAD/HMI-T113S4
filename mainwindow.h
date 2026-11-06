#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QString>

#include "devicestatus.h"

class QListWidget;
class QStackedWidget;
class QLabel;
class QTimer;
class BasePage;
class LogPanel;
class StatusDot;

// 左导航(QListWidget 180px, 9 项) + 内容区 QStackedWidget(设计基准 844x600)
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

private slots:
    void updateClock();
    void onNavChanged(int row);
    void onStatusChanged(const DeviceStatus &status);

private:
    void buildUi();
    void buildPages();
    void buildStatusBar();
    void subscribeStatus();

    QListWidget    *m_nav;
    QStackedWidget *m_stack;
    LogPanel       *m_log;
    QLabel         *m_clock;
    QTimer         *m_clockTimer;
    QHash<QString, StatusDot *> m_dots;    // device id → 状态灯
    QList<BasePage *> m_pages;
    QList<int>        m_navPages;          // 导航行 → 页面索引（总览不占导航）
    int             m_current;
};

#endif // MAINWINDOW_H
