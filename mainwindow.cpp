#include "mainwindow.h"

#include "basepage.h"
#include "cardbutton.h"
#include "logpanel.h"
#include "pagefactory.h"
#include "serviceregistry.h"
#include "softkeyboard.h"
#include "statusbus.h"
#include "statusdot.h"
#include "uimetrics.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_navPanel(nullptr)
    , m_nav(nullptr)
    , m_navToggle(nullptr)
    , m_stack(nullptr)
    , m_homeButton(nullptr)
    , m_log(nullptr)
    , m_keyboard(nullptr)
    , m_central(nullptr)
    , m_clock(nullptr)
    , m_clockTimer(nullptr)
    , m_navCollapsed(false)
    , m_navUserOverride(false)
    , m_compact(false)
    , m_current(-1)
{
    buildUi();
    buildPages();
    buildStatusBar();
    buildSoftKeyboard();
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
    resize(1024, 600);                                  // 桌面调试基准；板端 800x480 全屏
    setMinimumSize(UiMetrics::MinWindowWidth, UiMetrics::MinWindowHeight);

    QWidget *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralRoot"));
    m_central = central;
    QHBoxLayout *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左导航（可收起）----
    m_navPanel = new QWidget(central);
    m_navPanel->setObjectName(QStringLiteral("navPanel"));
    m_navPanel->setFixedWidth(UiMetrics::NavWidth);
    QVBoxLayout *leftLayout = new QVBoxLayout(m_navPanel);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(UiMetrics::PageSpacing);

    // 收起按钮：800x480 下功能区放不下，靠它把导航让出来
    m_navToggle = new QPushButton(QStringLiteral("☰"), m_navPanel);
    m_navToggle->setObjectName(QStringLiteral("navToggle"));
    m_navToggle->setFixedHeight(UiMetrics::NavItemHeight);
    m_navToggle->setFocusPolicy(Qt::NoFocus);
    m_navToggle->setCursor(Qt::PointingHandCursor);
    m_navToggle->setToolTip(tr("收起 / 展开导航"));
    connect(m_navToggle, &QPushButton::clicked, this, &MainWindow::toggleNav);
    leftLayout->addWidget(m_navToggle);

    CardButton *home = new CardButton(tr("总 览"), QString(), m_navPanel);
    home->setObjectName(QStringLiteral("homeButton"));
    home->setNavMode(true);              // 与导航列表同一套选中语义
    home->setHighlighted(true);          // 启动停在总览页
    home->setMinimumHeight(UiMetrics::NavItemHeight);   // 与导航项等高，纵向对齐
    home->setMaximumHeight(UiMetrics::NavItemHeight);
    connect(home, &QPushButton::clicked, this, [this]() { setCurrentPage(0); });
    leftLayout->addWidget(home);
    m_homeButton = home;

    m_nav = new QListWidget(m_navPanel);
    m_nav->setObjectName(QStringLiteral("navList"));
    m_nav->setFrameShape(QListWidget::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // 字号用 px 锚定：QSS 里已改 px，这里同步设定，避免继承到按 DPI 放大的全局 pt 字号
    QFont navFont = m_nav->font();
    navFont.setPixelSize(UiMetrics::NavPixelSize);
    m_nav->setFont(navFont);
    // 只监听用户点击，不用 currentRowChanged：
    // 后者反映的是列表内部 current 状态（Qt 会自动把第一项设为 current，
    // 显示/刷新时也可能补发），把它当"用户切页"会误触发，
    // 并且「切走再点回同一项」时 currentRow 没变化、信号压根不发。
    connect(m_nav, &QListWidget::itemClicked, this, &MainWindow::onNavItemClicked);
    connect(m_nav, &QListWidget::itemActivated, this, &MainWindow::onNavItemClicked);
    leftLayout->addWidget(m_nav, 1);
    root->addWidget(m_navPanel);

    // ---- 右侧内容 + 统一日志 ----
    QWidget *right = new QWidget(central);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 8, 8, 6);
    rightLayout->setSpacing(6);

    m_stack = new QStackedWidget(right);
    m_stack->setObjectName(QStringLiteral("contentStack"));
    // 不设 minimumWidth：800 宽下导航收起后只剩 744，
    // 再要求固定宽度会把窗口硬撑开，整体比例就乱了。交给布局自适应。
    rightLayout->addWidget(m_stack, 1);

    m_log = LogPanel::instance();       // 全局唯一，页面共用
    m_log->setFixedHeight(UiMetrics::LogHeight);
    rightLayout->addWidget(m_log);      // addWidget 会自动 reparent，不必再 setParent

    root->addWidget(right, 1);
    setCentralWidget(central);
}

// 导航展开 / 收起。收起时只留 56px 窄条，列表项显示首字，总览按钮缩成「总」。
// 幂等：重复调用只是重排一次，便于构造期/改尺寸后统一重刷。
void MainWindow::setNavCollapsed(bool collapsed)
{
    m_navCollapsed = collapsed;

    m_navPanel->setFixedWidth(collapsed ? UiMetrics::NavCollapsedWidth : UiMetrics::NavWidth);
    m_nav->setFixedWidth(collapsed ? UiMetrics::NavCollapsedWidth - 16
                                   : UiMetrics::NavListWidth);
    // QSS 给 #navList::item 设了 padding-left:10px，收起态下会把单字挤出去。
    // 收起时用内联样式覆盖（内联优先级高于 QSS 规则），展开时清空回到 QSS。
    m_nav->setStyleSheet(collapsed
        ? QStringLiteral("#navList::item { padding-left: 0px; padding-right: 0px; }")
        : QString());
    m_navToggle->setText(collapsed ? QStringLiteral("»") : QStringLiteral("☰"));

    if (m_homeButton) {
        m_homeButton->setText(collapsed ? tr("总") : tr("总 览"));
        m_homeButton->setToolTip(collapsed ? tr("总览") : QString());
    }

    for (int i = 0; i < m_nav->count(); ++i) {
        QListWidgetItem *item = m_nav->item(i);
        const QString full = item->data(Qt::UserRole).toString();
        if (full.isEmpty())
            continue;
        // 收起态取前两个字：单字会有歧义（总览/多媒体/以太网/ADB 都可能被压成同一个字）
        item->setText(collapsed ? full.left(2) : full);
        item->setToolTip(collapsed ? full : QString());
        item->setTextAlignment(collapsed ? (Qt::AlignVCenter | Qt::AlignHCenter)
                                         : (Qt::AlignVCenter | Qt::AlignLeft));
        item->setSizeHint(QSize(collapsed ? UiMetrics::NavCollapsedWidth - 16
                                          : UiMetrics::NavListWidth,
                                UiMetrics::NavItemHeight));
    }
}

// 按当前尺寸决定导航形态：用户手动切过就固定，否则窄屏自动收起
void MainWindow::applyNavForm()
{
    if (!m_navUserOverride)
        m_navCollapsed = width() < UiMetrics::AutoCollapseBelow;
    setNavCollapsed(m_navCollapsed);
}

// 480 高的板端屏：日志压到 84px，把高度还给功能区
void MainWindow::applyCompactMetrics()
{
    const bool compact = height() < 560;
    if (compact == m_compact)
        return;
    m_compact = compact;
    if (m_log)
        m_log->setFixedHeight(compact ? UiMetrics::LogHeightCompact : UiMetrics::LogHeight);
}

void MainWindow::toggleNav()
{
    m_navUserOverride = true;           // 手动切过就固定住，不再被 resizeEvent 覆盖
    setNavCollapsed(!m_navCollapsed);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    applyCompactMetrics();
    if (!m_navUserOverride)
        setNavCollapsed(width() < UiMetrics::AutoCollapseBelow);
    if (m_keyboard && m_keyboard->isVisible())
        m_keyboard->reposition();
}

// 软键盘作为 centralRoot 的子控件贴底停靠。
// 不用独立顶层窗口：板端 QPA 是 linuxfb，它只支持单窗口，
// 弹第二个窗口在实机上不是盖不住就是直接不显示。
void MainWindow::buildSoftKeyboard()
{
    if (!m_central)
        return;
    m_keyboard = new SoftKeyboard(m_central);
    m_keyboard->reposition();
    m_keyboard->attachGlobalFilter();   // 之后任何输入框被点中都会自动弹出
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
        item->setData(Qt::UserRole, entry.navText);      // 收起态显示首字时用
        item->setSizeHint(QSize(UiMetrics::NavListWidth, UiMetrics::NavItemHeight));
        item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        m_navPages << stackIndex;
    }

    // QListWidget 会把首个 item 自动设为 current。启动时当前页是总览，
    // 导航栏不该有任何选中项，这里显式清掉，避免"总览 + WiFi 同时高亮"。
    m_nav->setCurrentRow(-1);
    m_nav->clearSelection();

    // 页面建好后再按当前窗口尺寸摆一次导航形态（构造期宽度还没定下来）
    applyNavForm();
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
