#include "mainwindow.h"

#include "basepage.h"
#include "cardbutton.h"
#include "logpanel.h"
#include "pagefactory.h"
#include "serviceregistry.h"
#include "statusbus.h"
#include "statusdot.h"
#include "uimetrics.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_nav(nullptr)
    , m_stack(nullptr)
    , m_homeButton(nullptr)
    , m_log(nullptr)
    , m_clock(nullptr)
    , m_clockTimer(nullptr)
    , m_current(-1)
{
    buildUi();
    buildPages();
    buildStatusBar();
    subscribeStatus();

    setCurrentPage(0);

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start();
    updateClock();

    // 服务注册表接管周期探测：各服务按自己声明的间隔刷新（替代原先写死的 5s 定时器）
    ServiceRegistry::instance()->start(1000);
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("HMI-T113S4"));
    resize(1024, 600);                  // 板载屏 1024x600
    setMinimumSize(960, 540);           // 再小排版就散了；桌面拖动缩放时的下限

    QWidget *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralRoot"));
    QHBoxLayout *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左导航 ----
    QWidget *left = new QWidget(central);
    left->setObjectName(QStringLiteral("navPanel"));
    left->setFixedWidth(UiMetrics::NavWidth);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(UiMetrics::PageSpacing);

    CardButton *home = new CardButton(tr("总 览"), QString(), left);
    home->setObjectName(QStringLiteral("homeButton"));
    home->setNavMode(true);              // 与导航列表同一套选中语义
    home->setHighlighted(true);          // 启动停在总览页
    home->setMinimumHeight(UiMetrics::NavItemHeight);   // 与导航项等高，纵向对齐
    home->setMaximumHeight(UiMetrics::NavItemHeight);
    connect(home, &QPushButton::clicked, this, [this]() { setCurrentPage(0); });
    leftLayout->addWidget(home);
    m_homeButton = home;

    m_nav = new QListWidget(left);
    m_nav->setObjectName(QStringLiteral("navList"));
    m_nav->setFixedWidth(UiMetrics::NavListWidth);
    m_nav->setFrameShape(QListWidget::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // 只监听用户点击，不用 currentRowChanged：
    // 后者反映的是列表内部 current 状态（Qt 会自动把第一项设为 current，
    // 显示/刷新时也可能补发），把它当"用户切页"会误触发，
    // 并且「切走再点回同一项」时 currentRow 没变化、信号压根不发。
    connect(m_nav, &QListWidget::itemClicked, this, &MainWindow::onNavItemClicked);
    connect(m_nav, &QListWidget::itemActivated, this, &MainWindow::onNavItemClicked);
    leftLayout->addWidget(m_nav, 1);
    root->addWidget(left);

    // ---- 右侧内容 + 统一日志 ----
    QWidget *right = new QWidget(central);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 8, 8, 6);
    rightLayout->setSpacing(6);

    m_stack = new QStackedWidget(right);
    m_stack->setObjectName(QStringLiteral("contentStack"));
    // 不设 minimumWidth：1024 宽下导航 168 + 两侧边距 16 只剩 840，
    // 再要求 844 会把窗口硬撑到 1040，整体比例就乱了。交给布局自适应。
    rightLayout->addWidget(m_stack, 1);

    m_log = LogPanel::instance();       // 全局唯一，页面共用
    m_log->setFixedHeight(UiMetrics::LogHeight);
    rightLayout->addWidget(m_log);      // addWidget 会自动 reparent，不必再 setParent

    root->addWidget(right, 1);
    setCentralWidget(central);
}

void MainWindow::buildPages()
{
    // 页面全部来自注册表：新增页面只需新建文件 + 一行 HMI_REGISTER_PAGE，
    // 本函数与导航都不需要改（开闭原则）。
    const QList<PageFactory::Entry> entries = PageFactory::instance()->entries();

    // 建导航项期间必须阻断信号：QListWidget 加入第一个 item 时会自动把它设为
    // current 并发出 currentRowChanged(0)，此时页面还没建完，会把栈切到第一个
    // 功能页（WiFi），导致「启动后停在 WiFi 而不是总览」。当前页由末尾的
    // setCurrentPage(0) 统一决定。
    const QSignalBlocker navBlocker(m_nav);

    for (int i = 0; i < entries.size(); ++i) {
        const PageFactory::Entry &entry = entries.at(i);
        BasePage *page = PageFactory::instance()->create(entry.id, this);
        if (!page)
            continue;

        const int stackIndex = m_pages.size();
        m_pages << page;
        m_stack->addWidget(page);

        if (entry.navText.isEmpty())
            continue;                       // 总览页不占导航项

        QListWidgetItem *item = new QListWidgetItem(entry.navText, m_nav);
        item->setSizeHint(QSize(UiMetrics::NavListWidth, UiMetrics::NavItemHeight));
        item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        m_navPages << stackIndex;
    }

    // QListWidget 会把首个 item 自动设为 current。启动时当前页是总览，
    // 导航栏不该有任何选中项，这里显式清掉，避免"总览 + WiFi 同时高亮"。
    m_nav->setCurrentRow(-1);
    m_nav->clearSelection();
}

void MainWindow::buildStatusBar()
{
    QStatusBar *bar = statusBar();
    bar->setObjectName(QStringLiteral("statusBar"));
    bar->setSizeGripEnabled(false);
    bar->setFixedHeight(UiMetrics::StatusBarHeight);

    m_clock = new QLabel(bar);
    m_clock->setObjectName(QStringLiteral("clockLabel"));

    // 状态灯与 device id 绑定：新增设备只在服务里 publish，这里加一行即可
    StatusDot *wifiDot = new StatusDot(tr("WiFi"), bar);
    StatusDot *ethDot  = new StatusDot(tr("ETH"), bar);
    StatusDot *adbDot  = new StatusDot(tr("ADB"), bar);
    m_dots.insert(QStringLiteral("wifi"), wifiDot);
    m_dots.insert(QStringLiteral("eth"), ethDot);
    m_dots.insert(QStringLiteral("adb"), adbDot);

    bar->addPermanentWidget(m_clock);
    bar->addPermanentWidget(wifiDot);
    bar->addPermanentWidget(ethDot);
    bar->addPermanentWidget(adbDot);
}

void MainWindow::subscribeStatus()
{
    connect(StatusBus::instance(), &StatusBus::changed, this, &MainWindow::onStatusChanged);

    const QList<DeviceStatus> known = StatusBus::instance()->snapshot();
    for (int i = 0; i < known.size(); ++i)
        onStatusChanged(known.at(i));
}

void MainWindow::onStatusChanged(const DeviceStatus &status)
{
    StatusDot *dot = m_dots.value(status.device);
    if (!dot)
        return;

    switch (status.state) {
    case DeviceReady: dot->setLevel(StatusDot::On);    break;
    case DeviceBusy:  dot->setLevel(StatusDot::Busy);  break;
    case DeviceError: dot->setLevel(StatusDot::Error); break;
    case DeviceOff:
    case DeviceUnknown:
    default:          dot->setLevel(StatusDot::Off);   break;
    }
    dot->setText(status.detail.isEmpty() ? deviceStateName(status.state) : status.detail);
}

void MainWindow::onNavItemClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    const int row = m_nav->row(item);
    if (row < 0 || row >= m_navPages.size())
        return;
    setCurrentPage(m_navPages.at(row));
}

void MainWindow::setCurrentPage(int stackIndex)
{
    if (stackIndex < 0 || stackIndex >= m_stack->count() || stackIndex == m_current)
        return;

    if (m_current >= 0 && m_current < m_pages.size())
        m_pages.at(m_current)->onLeave();

    m_stack->setCurrentIndex(stackIndex);
    m_current = stackIndex;
    m_pages.at(stackIndex)->onEnter();

    // 导航选中态与当前页严格同步。
    // 关键：切到总览（row = -1）必须用 setCurrentRow(-1) 把 current item 真正清掉。
    // 只调 clearSelection() 是去不掉 current 的——高亮没了，currentRow 仍停在
    // 上一个功能页，于是「总览 → 再点回原来那一项」时 currentRowChanged 认为
    // 值没变、压根不触发，表现为第二次点同一项不跳转。
    const int row = m_navPages.indexOf(stackIndex);
    QSignalBlocker blocker(m_nav);
    if (m_nav->currentRow() != row)
        m_nav->setCurrentRow(row);
    if (row < 0)
        m_nav->clearSelection();

    // 总览按钮的高亮跟着当前页走：全窗口任何时刻只有一个"选中"视觉
    if (m_homeButton)
        m_homeButton->setHighlighted(stackIndex == 0);
}

void MainWindow::updateClock()
{
    m_clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
}
