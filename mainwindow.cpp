#include "mainwindow.h"

#include "appversion.h"
#include "baseview.h"
#include "homeview.h"
#include "settingview.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_stack(nullptr)
    , m_titleLabel(nullptr)
    , m_clockLabel(nullptr)
    , m_stateLabel(nullptr)
    , m_versionLabel(nullptr)
    , m_clockTimer(nullptr)
    , m_currentIndex(-1)
{
    buildUi();

    HomeView *home = new HomeView(this);
    SettingView *setting = new SettingView(this);
    addView(home);
    addView(setting);

    connect(home, &HomeView::requestPage, this, &MainWindow::setCurrentIndex);

    setCurrentIndex(0);
}

MainWindow::~MainWindow()
{
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral(APP_NAME));
    resize(1024, 600);

    QWidget *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralRoot"));

    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(createTitleBar());

    m_stack = new QStackedWidget(central);
    m_stack->setObjectName(QStringLiteral("contentStack"));
    rootLayout->addWidget(m_stack, 1);

    rootLayout->addWidget(createStatusBar());

    setCentralWidget(central);

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start();
    updateClock();
}

QWidget *MainWindow::createTitleBar()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("titleBar"));
    bar->setFixedHeight(48);

    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(12);

    m_titleLabel = new QLabel(QStringLiteral(APP_NAME), bar);
    m_titleLabel->setObjectName(QStringLiteral("titleLabel"));

    m_clockLabel = new QLabel(bar);
    m_clockLabel->setObjectName(QStringLiteral("clockLabel"));

    layout->addWidget(m_titleLabel);
    layout->addStretch(1);
    layout->addWidget(m_clockLabel);

    return bar;
}

QWidget *MainWindow::createStatusBar()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("statusBar"));
    bar->setFixedHeight(28);

    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(12);

    m_stateLabel = new QLabel(QStringLiteral("通信状态：未连接"), bar);
    m_stateLabel->setObjectName(QStringLiteral("stateLabel"));

    m_versionLabel = new QLabel(QStringLiteral("v") + QStringLiteral(APP_VERSION), bar);
    m_versionLabel->setObjectName(QStringLiteral("versionLabel"));

    layout->addWidget(m_stateLabel);
    layout->addStretch(1);
    layout->addWidget(m_versionLabel);

    return bar;
}

void MainWindow::addView(BaseView *view)
{
    if (!view || !m_stack)
        return;
    m_stack->addWidget(view);
}

int MainWindow::currentIndex() const
{
    return m_currentIndex;
}

void MainWindow::setCurrentIndex(int index)
{
    if (!m_stack || index < 0 || index >= m_stack->count())
        return;
    if (index == m_currentIndex)
        return;

    if (m_currentIndex >= 0) {
        if (BaseView *oldView = qobject_cast<BaseView *>(m_stack->widget(m_currentIndex)))
            oldView->onLeave();
    }

    m_stack->setCurrentIndex(index);
    m_currentIndex = index;

    if (BaseView *view = qobject_cast<BaseView *>(m_stack->widget(index))) {
        view->onEnter();
        m_titleLabel->setText(QStringLiteral(APP_NAME) + QStringLiteral("  |  ") + view->title());
    }

    emit currentViewChanged(index);
}

void MainWindow::updateClock()
{
    if (m_clockLabel)
        m_clockLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
}

