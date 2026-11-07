#include "basepage.h"
#include "cardbutton.h"
#include "uimetrics.h"

#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QGridLayout>
#include <QVBoxLayout>

BasePage::BasePage(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_status(nullptr)
    , m_body(nullptr)
    , m_title(title)
    , m_hint(nullptr)
    , m_scroll(nullptr)
    , m_initialized(false)
{
    setObjectName(QStringLiteral("basePage"));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(UiMetrics::PageMarginH, UiMetrics::PageMarginTop,
                             UiMetrics::PageMarginH, UiMetrics::PageMarginBot);
    root->setSpacing(UiMetrics::PageSpacing);

    QLabel *titleLabel = new QLabel(m_title, this);
    titleLabel->setObjectName(QStringLiteral("pageTitle"));
    QFont f = titleLabel->font();
    f.setPointSize(UiMetrics::TitlePointSize);
    f.setBold(true);
    titleLabel->setFont(f);           // 只改字号/粗细，字体族交 fontconfig
    root->addWidget(titleLabel);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("pageHint"));
    m_hint->setWordWrap(true);
    m_hint->hide();
    root->addWidget(m_hint);

    m_status = new QLabel(tr("就绪"), this);
    m_status->setObjectName(QStringLiteral("pageStatus"));
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(UiMetrics::StatusHeight);
    root->addWidget(m_status);

    QWidget *body = new QWidget(this);
    m_body = new QVBoxLayout(body);
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(UiMetrics::PageSpacing);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidget(body);
    root->addWidget(m_scroll, 1);
}

QString BasePage::title() const
{
    return m_title;
}

void BasePage::initialize()
{
    if (m_initialized)
        return;
    m_initialized = true;
    buildContent();      // 子类钩子（虚函数，此时对象已完整构造，能正确派发）

    // 内容末尾补一个弹性占位：否则 QVBoxLayout 会把剩余高度均摊给各控件，
    // 让一个普通 QLabel 撑成一大块空白（I2C 页的「尚未扫描」就是这样）。
    // 加在 buildContent 之后，保证它永远是最后一项。
    m_body->addStretch(1);
}

bool BasePage::isInitialized() const
{
    return m_initialized;
}

void BasePage::buildContent()
{
}

void BasePage::onEnter()
{
}

void BasePage::onLeave()
{
}

void BasePage::setHint(const QString &text)
{
    m_hint->setText(text);
    m_hint->setVisible(!text.isEmpty());
}

void BasePage::setStatus(const QString &text)
{
    m_status->setText(text);
}

void BasePage::addWidget(QWidget *widget)
{
    if (widget)
        m_body->addWidget(widget);
}

QGridLayout *BasePage::addGrid()
{
    QWidget *holder = new QWidget(this);
    QGridLayout *grid = new QGridLayout(holder);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(UiMetrics::PageSpacing);
    grid->setColumnStretch(0, 1);       // 两列等宽，卡片边界沿同一条线对齐
    grid->setColumnStretch(1, 1);
    m_body->addWidget(holder);
    return grid;
}

CardButton *BasePage::newButton(const QString &text, const std::function<void()> &fn,
                                const QString &description, bool accent)
{
    CardButton *btn = new CardButton(text, description, this);
    btn->setAccent(accent);
    if (fn)
        connect(btn, &QPushButton::clicked, this, fn);
    return btn;
}

CardButton *BasePage::addButton(const QString &text, const std::function<void()> &fn,
                                const QString &description, bool accent)
{
    CardButton *btn = newButton(text, fn, description, accent);
    m_body->addWidget(btn);
    return btn;
}
