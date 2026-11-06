#include "mainwindow.h"

#include "basepage.h"
#include "cardbutton.h"
#include "logpanel.h"
#include "pagefactory.h"
#include "serviceregistry.h"
#include "statusbus.h"
#include "statusdot.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHBoxLayout>
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

    QWidget *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralRoot"));
    QHBoxLayout *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左导航 ----
    QWidget *left = new QWidget(central);
    left->setObjectName(QStringLiteral("navPanel"));
    left->setFixedWidth(180);           // 硬约定 180px
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(8);

    CardButton *home = new CardButton(tr("总 览"), left);
    home->setObjectName(QStringLiteral("homeButton"));
    home->setAccent(true);
    connect(home, &QPushButton::clicked, this, [this]() { setCurrentPage(0); });
    leftLayout->addWidget(home);

    m_nav = new QListWidget(left);
    m_nav->setObjectName(QStringLiteral("navList"));
    m_nav->setFixedWidth(164);
    m_nav->setFrameShape(QListWidget::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::onNavChanged);
    leftLayout->addWidget(m_nav, 1);
    root->addWidget(left);

    // ---- 右侧内容 + 统一日志 ----
    QWidget *right = new QWidget(central);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 8, 8, 4);
    rightLayout->setSpacing(6);

    m_stack = new QStackedWidget(right);
    m_stack->setObjectName(QStringLiteral("contentStack"));
    m_stack->setMinimumWidth(844);      // 设计基准 844x600，实际由布局自适应
    rightLayout->addWidget(m_stack, 1);

    m_log = LogPanel::instance();       // 全局唯一，页面共用
    m_log->setFixedHeight(150);
    m_log->setParent(right);
    rightLayout->addWidget(m_log);

    root->addWidget(right, 1);
    setCentralWidget(central);
}

void MainWindow::buildPages()
{
    // 页面全部来自注册表：新增页面只需新建文件 + 一行 HMI_REGISTER_PAGE，
    // 本函数与导航都不需要改（开闭原则）。
    const QList<PageFactory::Entry> entries = PageFactory::instance()->entries();
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
        item->setSizeHint(QSize(164, 56));  // 触摸友好 ≥56px
        item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        m_navPages << stackIndex;
    }
}

void MainWindow::buildStatusBar()
{
    QStatusBar *bar = statusBar();
    bar->setObjectName(QStringLiteral("statusBar"));
    bar->setSizeGripEnabled(false);
    bar->setFixedHeight(36);

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

void MainWindow::onNavChanged(int row)
{
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

    const int row = m_navPages.indexOf(stackIndex);
    QSignalBlocker blocker(m_nav);
    if (row >= 0) {
        if (m_nav->currentRow() != row)
            m_nav->setCurrentRow(row);
    } else {
        m_nav->clearSelection();
    }
}

void MainWindow::updateClock()
{
    m_clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
}
