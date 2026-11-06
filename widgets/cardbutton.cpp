#include "cardbutton.h"

#include <QPainter>
#include <QPen>

CardButton::CardButton(const QString &text, const QString &description, QWidget *parent)
    : QPushButton(text, parent)
    , m_description(description)
    , m_accent(false)
{
    setObjectName(QStringLiteral("cardButton"));
    setMinimumHeight(56);                 // 硬约定：触摸按钮 ≥56px
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QString CardButton::description() const
{
    return m_description;
}

void CardButton::setDescription(const QString &text)
{
    m_description = text;
    update();
}

void CardButton::setAccent(bool accent)
{
    m_accent = accent;
    update();
}

QSize CardButton::sizeHint() const
{
    return QSize(200, m_description.isEmpty() ? 56 : 72);
}

QSize CardButton::minimumSizeHint() const
{
    return QSize(120, 56);
}

void CardButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor bg(QStringLiteral("#FFFFFF"));
    QColor border(QStringLiteral("#C8D5EA"));
    QColor titleColor(QStringLiteral("#2F3A4A"));
    if (m_accent) {
        bg = QColor(QStringLiteral("#4187F0"));
        border = QColor(QStringLiteral("#2F6FD0"));
        titleColor = QColor(QStringLiteral("#FFFFFF"));
    }
    if (isDown())
        bg = m_accent ? QColor(QStringLiteral("#2559A8")) : QColor(QStringLiteral("#DCE8FA"));
    else if (!isEnabled())
        bg = QColor(QStringLiteral("#F0F0F0"));
    else if (underMouse() && !m_accent)
        bg = QColor(QStringLiteral("#EEF4FF"));

    const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(QPen(border, 1));
    p.setBrush(bg);
    p.drawRoundedRect(box, 6, 6);

    const int left = 16;
    QFont titleFont = font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    QFont descFont = font();
    descFont.setPointSize(11);

    if (m_description.isEmpty()) {
        p.setPen(titleColor);
        p.setFont(titleFont);
        p.drawText(rect().adjusted(left, 0, -left, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
        return;
    }

    const QRect top(rect().left() + left, rect().top() + 6, rect().width() - 2 * left, rect().height() / 2);
    const QRect bottom(rect().left() + left, rect().top() + rect().height() / 2,
                       rect().width() - 2 * left, rect().height() / 2 - 6);
    p.setPen(titleColor);
    p.setFont(titleFont);
    p.drawText(top, Qt::AlignBottom | Qt::AlignLeft, text());
    p.setPen(m_accent ? QColor(QStringLiteral("#DCE8FF")) : QColor(QStringLiteral("#6B778C")));
    p.setFont(descFont);
    p.drawText(bottom, Qt::AlignTop | Qt::AlignLeft, m_description);
}
