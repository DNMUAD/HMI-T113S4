#include "softkeyboard.h"
#include "uimetrics.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

// 键盘上的键一律 NoFocus：点了键以后输入框仍然是焦点控件，
// 光标不闪断、也不用每敲一下就把焦点抢回来。
//
// 注意 & ：QPushButton 把 & 当助记符标记，单个 & 会被吞掉、
// 后面的字符变下划线（数字页的 "&" 键就是这样成了空白）。
// 键帽要显示字面 &，必须写成 &&。
QString escapeMnemonic(const QString &text)
{
    QString out = text;
    out.replace(QStringLiteral("&"), QStringLiteral("&&"));
    return out;
}

QPushButton *makeKey(QWidget *parent, const QString &text, const char *objectName)
{
    QPushButton *btn = new QPushButton(escapeMnemonic(text), parent);
    btn->setObjectName(QString::fromLatin1(objectName));
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(UiMetrics::SoftKeyHeight);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return btn;
}

// QAbstractSpinBox::lineEdit() 是 protected，外部拿不到。
// QSpinBox 内部那个行编辑是它的子对象，用 findChild 取是官方支持的公开途径
// （canpage / i2cpage 的数字框都靠这条路径接收输入）。
QLineEdit *spinBoxEditor(QAbstractSpinBox *spin)
{
    return spin ? spin->findChild<QLineEdit *>() : nullptr;
}

// 沿父链找「真正接收文字的那个控件」：
// 点击 QPlainTextEdit 的 viewport 时事件目标是 viewport，
// 但焦点控件和插入接口都在 QPlainTextEdit 上。
QWidget *textInputAncestor(QWidget *w)
{
    while (w) {
        if (qobject_cast<QLineEdit *>(w) || qobject_cast<QPlainTextEdit *>(w)
                || qobject_cast<QTextEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w))
            return w;
        w = w->parentWidget();
    }
    return nullptr;
}

} // namespace

SoftKeyboard::SoftKeyboard(QWidget *parent)
    : QWidget(parent)
    , m_pages(nullptr)
    , m_filterInstalled(false)
    , m_pwField(false)
{
    setObjectName(QStringLiteral("softKeyboard"));
    setFixedHeight(UiMetrics::SoftKeyboardHeight);
    // 键盘必须是完全不透明的：它盖在页面内容之上，
    // 底下的按钮和日志文字一旦透出来就完全没法看。
    // 普通 QWidget 默认不画 QSS 的 background-color，所以要下面两件事：
    //   ① autoFillBackground —— 让 widget 参与背景填充
    //   ② paintEvent 里显式 fillRect —— 否则填的还是调色板默认色，不是 QSS 色
    setAutoFillBackground(true);
    hide();                             // 默认收起，第一次点输入框才出现

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(UiMetrics::SoftKeyMargin, UiMetrics::SoftKeyMargin,
                             UiMetrics::SoftKeyMargin, UiMetrics::SoftKeyMargin);
    root->setSpacing(UiMetrics::SoftKeySpacing);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("softKeyboardPages"));
    m_pages->addWidget(buildLettersPage(false));    // PageLower
    m_pages->addWidget(buildLettersPage(true));     // PageUpper
    m_pages->addWidget(buildDigitsPage());          // PageDigits
    root->addWidget(m_pages);
}

// 字母键盘。upper 决定键帽显示大写还是小写、以及插入的字符 ——
// 两页都预建好，切 Shift 只是 setCurrentIndex，不重建按钮。
QWidget *SoftKeyboard::buildLettersPage(bool upper)
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(UiMetrics::SoftKeySpacing);

    const char *rows[2][2] = {
        { "qwertyuiop", "asdfghjkl" },      // 小写
        { "QWERTYUIOP", "ASDFGHJKL" }       // 大写
    };
    const int variant = upper ? 1 : 0;

    for (int r = 0; r < 2; ++r) {
        QHBoxLayout *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(UiMetrics::SoftKeySpacing);
        const QString chars = QString::fromLatin1(rows[variant][r]);
        for (int i = 0; i < chars.size(); ++i) {
            QPushButton *key = makeKey(page, chars.at(i), "softKey");
            const QString text = chars.at(i);
            connect(key, &QPushButton::clicked, this, [this, text]() { insertText(text); });
            row->addWidget(key, 1);
        }
        // 第二行只有 9 键、第一行 10 键。用 stretch 补齐右侧空档（不占 widget），
        // 两行单键宽度才会一致；直接补一个 addSpacing 会让第二行每键宽一截。
        if (r == 1)
            row->addStretch(1);
        col->addLayout(row);
    }

    // 第三行：Shift + 8 个字母 + 退格，正好 10 列
    QHBoxLayout *row3 = new QHBoxLayout;
    row3->setContentsMargins(0, 0, 0, 0);
    row3->setSpacing(UiMetrics::SoftKeySpacing);

    QPushButton *shift = makeKey(page, upper ? tr("小写") : tr("大写"),
                                 "softKeyAccent");
    shift->setToolTip(upper ? tr("切到小写") : tr("切到大写（单次）"));
    connect(shift, &QPushButton::clicked, this, [this, upper]() {
        m_pages->setCurrentIndex(upper ? PageLower : PageUpper);
    });
    row3->addWidget(shift, 1);

    const QString middle = QStringLiteral("zxcvbnm");
    for (int i = 0; i < middle.size(); ++i) {
        QPushButton *key = makeKey(page, middle.at(i), "softKey");
        const QString text = middle.at(i);
        connect(key, &QPushButton::clicked, this, [this, text]() { insertText(text); });
        row3->addWidget(key, 1);
    }

    QPushButton *back = makeKey(page, tr("退格"), "softKeyAccent");
    back->setToolTip(tr("退格"));
    connect(back, &QPushButton::clicked, this, &SoftKeyboard::doBackspace);
    row3->addWidget(back, 1);
    col->addLayout(row3);

    col->addLayout(buildBottomRow(page));
    return page;
}

// 数字 / 符号键盘。密码大多是数字，切到密码框时默认就用这一页。
QWidget *SoftKeyboard::buildDigitsPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(UiMetrics::SoftKeySpacing);

    // 每行 9 个字符，第 10 列留给退格 —— 数字页必须有自己的退格键：
    // 密码框默认就停在数字页，如果退格只在字母页，输错一位得先切页才能删。
    // 用 3 排数字符号（而不是 4 排）保证和字母页同样高，键盘高度不用随页变化。
    const QStringList rows = QStringList()
            << QStringLiteral("123456789")
            << QStringLiteral("@#$%^&*()")
            << QStringLiteral("-0_=+,.;:");
    for (int r = 0; r < rows.size(); ++r) {
        QHBoxLayout *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(UiMetrics::SoftKeySpacing);
        const QString chars = rows.at(r);
        for (int i = 0; i < chars.size(); ++i) {
            QPushButton *key = makeKey(page, chars.at(i), "softKey");
            const QString text = chars.at(i);
            connect(key, &QPushButton::clicked, this, [this, text]() { insertText(text); });
            row->addWidget(key, 1);
        }

        if (r == 0) {
            QPushButton *back = makeKey(page, tr("退格"), "softKeyAccent");
            back->setToolTip(tr("退格"));
            connect(back, &QPushButton::clicked, this, &SoftKeyboard::doBackspace);
            row->addWidget(back, 1);
        } else {
            row->addStretch(1);         // 右侧补空档，单键宽度与首行一致
        }
        col->addLayout(row);
    }

    col->addLayout(buildBottomRow(page));
    return page;
}

// 每页共用的底行：[切页] [显示/隐藏] [空格] [清空] [确定]
// 「显示/隐藏」只有在密码框上才有意义，其余场合隐藏（布局会自动回收空间）。
QHBoxLayout *SoftKeyboard::buildBottomRow(QWidget *page)
{
    QHBoxLayout *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UiMetrics::SoftKeySpacing);

    QPushButton *switchPage = makeKey(page, QStringLiteral("?123"), "softKeyAccent");
    switchPage->setToolTip(tr("切换字母 / 数字符号"));
    connect(switchPage, &QPushButton::clicked, this, [this]() {
        const bool letters = m_pages->currentIndex() != PageDigits;
        m_pages->setCurrentIndex(letters ? PageDigits : PageLower);
    });
    row->addWidget(switchPage, 14);

    QPushButton *reveal = makeKey(page, tr("显示"), "softKeyAccent");
    reveal->setToolTip(tr("显示 / 隐藏密码"));
    reveal->hide();
    connect(reveal, &QPushButton::clicked, this, &SoftKeyboard::doContext);
    row->addWidget(reveal, 14);
    m_revealKeys.append(reveal);

    QPushButton *space = makeKey(page, tr("空格"), "softKey");
    connect(space, &QPushButton::clicked, this, [this]() { insertText(QStringLiteral(" ")); });
    row->addWidget(space, 34);

    QPushButton *clear = makeKey(page, tr("清空"), "softKeyAccent");
    connect(clear, &QPushButton::clicked, this, &SoftKeyboard::doClear);
    row->addWidget(clear, 16);

    QPushButton *done = makeKey(page, tr("确定"), "softKeyPrimary");
    connect(done, &QPushButton::clicked, this, &SoftKeyboard::finish);
    row->addWidget(done, 22);

    return row;
}

void SoftKeyboard::attachGlobalFilter()
{
    if (m_filterInstalled)
        return;
    if (QApplication *app = qobject_cast<QApplication *>(QApplication::instance())) {
        app->installEventFilter(this);
        m_filterInstalled = true;
    }
}

bool SoftKeyboard::isAttached() const
{
    return isVisible() && !m_target.isNull();
}

void SoftKeyboard::attachTo(QWidget *target)
{
    if (!isTextInput(target))
        return;

    const bool targetChanged = (m_target.data() != target);
    m_target = target;

    // 密码框身份只在切换目标时采样一次（见 m_pwField 的注释）
    QLineEdit *edit = qobject_cast<QLineEdit *>(target);
    if (targetChanged)
        m_pwField = edit && edit->echoMode() != QLineEdit::Normal;

    // 密码框默认给数字页：板端输密码九成是纯数字，少一次切页
    if (targetChanged) {
        m_pages->setCurrentIndex(m_pwField ? PageDigits : PageLower);
    }
    syncContextKeys();

    if (!isVisible()) {
        reposition();
        raise();                    // 盖在页面和日志之上
        show();
        emit shownFor(target);
    }
    ensureTargetVisible();
}

void SoftKeyboard::dismiss()
{
    if (!isVisible())
        return;
    hide();
    m_target = nullptr;
    emit dismissed();
}

void SoftKeyboard::reposition()
{
    QWidget *host = parentWidget();
    if (!host)
        return;
    const int h = UiMetrics::SoftKeyboardHeight;
    // 贴底停靠、整宽覆盖：linuxfb 是单窗口平台，只能靠"盖住一块"来腾地方，
    // 不能像桌面那样弹独立的小窗
    setGeometry(0, qMax(0, host->height() - h), host->width(), h);
}

bool SoftKeyboard::isTextInput(QWidget *w)
{
    return textInputAncestor(w) != nullptr;
}

bool SoftKeyboard::isInsideKeyboard(const QWidget *w, const QWidget *keyboard)
{
    for (const QWidget *p = w; p; p = p->parentWidget()) {
        if (p == keyboard)
            return true;
    }
    return false;
}

bool SoftKeyboard::targetIsLineEdit() const
{
    return qobject_cast<QLineEdit *>(m_target.data()) != nullptr;
}

// 「显示 / 隐藏」键的文案要跟输入框的实际状态走。
// 判断「是不是密码框」不能用 echoMode 反推 —— 用户点过「显示」之后
// echoMode 就是 Normal 了，反推会以为它不是密码框，开关键会自己消失。
// 所以在 attachTo 时采样一次存进 m_pwField（见那里注释）。
void SoftKeyboard::syncContextKeys()
{
    QLineEdit *edit = qobject_cast<QLineEdit *>(m_target.data());
    const bool pw = m_pwField && edit;
    const bool normal = pw && edit->echoMode() == QLineEdit::Normal;
    for (int i = 0; i < m_revealKeys.size(); ++i) {
        QPushButton *key = m_revealKeys.at(i);
        key->setVisible(pw);
        key->setText(pw ? (normal ? tr("隐藏") : tr("显示")) : QString());
    }
}

void SoftKeyboard::insertText(const QString &text)
{
    if (!m_target)
        return;
    QWidget *w = m_target.data();

    if (QLineEdit *edit = qobject_cast<QLineEdit *>(w)) {
        edit->insert(text);          // insert() 自带光标位置与选中替换语义
        return;
    }
    if (QPlainTextEdit *edit = qobject_cast<QPlainTextEdit *>(w)) {
        edit->insertPlainText(text);
        return;
    }
    if (QTextEdit *edit = qobject_cast<QTextEdit *>(w)) {
        edit->insertPlainText(text);
        return;
    }
    if (QAbstractSpinBox *spin = qobject_cast<QAbstractSpinBox *>(w)) {
        // QAbstractSpinBox 有 lineEdit()，数字框直接往内嵌行编辑里塞
        if (QLineEdit *edit = spinBoxEditor(spin))
            edit->insert(text);
        return;
    }
}

void SoftKeyboard::doBackspace()
{
    if (!m_target)
        return;
    QWidget *w = m_target.data();

    if (QLineEdit *edit = qobject_cast<QLineEdit *>(w)) {
        edit->backspace();
        return;
    }
    if (QPlainTextEdit *edit = qobject_cast<QPlainTextEdit *>(w)) {
        edit->textCursor().deletePreviousChar();
        return;
    }
    if (QTextEdit *edit = qobject_cast<QTextEdit *>(w)) {
        edit->textCursor().deletePreviousChar();
        return;
    }
    if (QAbstractSpinBox *spin = qobject_cast<QAbstractSpinBox *>(w)) {
        if (QLineEdit *edit = spinBoxEditor(spin))
            edit->backspace();
        return;
    }
}

void SoftKeyboard::doClear()
{
    if (!m_target)
        return;
    QWidget *w = m_target.data();

    if (QLineEdit *edit = qobject_cast<QLineEdit *>(w)) {
        edit->clear();
        return;
    }
    if (QPlainTextEdit *edit = qobject_cast<QPlainTextEdit *>(w)) {
        edit->clear();
        return;
    }
    if (QTextEdit *edit = qobject_cast<QTextEdit *>(w)) {
        edit->clear();
        return;
    }
    if (QAbstractSpinBox *spin = qobject_cast<QAbstractSpinBox *>(w)) {
        if (QLineEdit *edit = spinBoxEditor(spin))
            edit->clear();
        return;
    }
}

// 「显示 / 隐藏」：板端没有物理键盘，密码框输错了看不见，
// 必须能在触摸屏上临时切回明文核对。
void SoftKeyboard::doContext()
{
    QLineEdit *edit = qobject_cast<QLineEdit *>(m_target.data());
    if (!edit || !m_pwField)
        return;
    const bool hidden = (edit->echoMode() != QLineEdit::Normal);
    edit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
    syncContextKeys();          // 文案统一由它刷新，别在两处各写一遍
}

void SoftKeyboard::finish()
{
    QWidget *w = m_target.data();
    dismiss();
    if (!w)
        return;
    // 提一下编辑完成，让页面上的「连接」等按钮逻辑能感知输入结束
    if (QLineEdit *edit = qobject_cast<QLineEdit *>(w))
        emit edit->editingFinished();
}

// 键盘盖住底部 185px 时，把目标输入框滚到可见区，
// 否则在 800x480 上会"看不见自己在输什么"。
void SoftKeyboard::ensureTargetVisible()
{
    if (!m_target)
        return;
    QWidget *w = m_target.data();
    for (QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
        QScrollArea *area = qobject_cast<QScrollArea *>(p);
        if (!area)
            continue;
        area->ensureWidgetVisible(w, 0, UiMetrics::SoftKeyboardHeight + 12);
        return;
    }
}

// 显式铺不透明底色。原因见构造函数：普通 QWidget 不会自动把 QSS 的
// background-color 画出来，只靠 setAutoFillBackground 填的是调色板默认色。
// 这里和 style.qss 的 #softKeyboard 背景保持一致，改色时两处一起改。
void SoftKeyboard::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QColor(0xE8, 0xED, 0xF5));
    // 顶部那条蓝线与 QSS 的 border-top 呼应，画在这里避免依赖边框绘制
    painter.fillRect(QRect(0, 0, width(), 2), QColor(0x41, 0x87, 0xF0));
}

bool SoftKeyboard::eventFilter(QObject *watched, QEvent *event)
{
    // 先按"点中/聚焦到哪个输入控件"决定弹出，
    // 再按"点到别处去了"决定收起 —— 两条互斥，别同时成立。
    switch (event->type()) {
    case QEvent::FocusIn: {
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (w && !isInsideKeyboard(w, this)) {
            if (QWidget *input = textInputAncestor(w))
                attachTo(input);
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        if (!isVisible())
            break;
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (!w)
            break;
        if (isInsideKeyboard(w, this))
            break;                       // 点的是键盘自己的键：保持
        if (textInputAncestor(w))
            break;                       // 点另一个输入框：attachTo 会接管
        dismiss();                       // 点到页面空白/按钮：收起
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
