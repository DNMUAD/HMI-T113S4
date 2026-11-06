#include "statusdot.h"

#include <QPainter>

StatusDot::StatusDot(const QString &label, QWidget *parent)
    : QWidget(parent)
    , m_label(label)
    , m_level(Off)
{
    setObjectName(QStringLiteral("statusDot"));
    setMinimumHeight(28);
}

void StatusDot::setLevel(Level level)
{
    if (m_level == level)
        return;
    m_level = level;
    update();
    emit stateChanged(m_level);
}

StatusDot::Level StatusDot::level() const
{
    return m_level;
}

void StatusDot::setText(const QString &text)
{
    m_text = text;
    updateGeometry();
    update();
}

QString StatusDot::text() const
{
    return m_text;
}

QSize StatusDot::sizeHint() const
{
    const int w = 24 + fontMetrics().width(m_label + QStringLiteral("  ") + m_text) + 16;
    return QSize(qMax(90, w), 30);
}

void StatusDot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor color(QStringLiteral("#9AA5B1"));   // Off
    switch (m_level) {
    case On:    color = QColor(QStringLiteral("#1E8E3E")); break;
    case Busy:  color = QColor(QStringLiteral("#4187F0")); break;
    case Warn:  color = QColor(QStringLiteral("#E8A317")); break;
    case Error: color = QColor(QStringLiteral("#C62828")); break;
    default: break;
    }

    const int r = 10;
    const int cy = height() / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(8 + r / 2.0, cy), r / 2.0, r / 2.0);

    p.setPen(palette().color(QPalette::WindowText));
    p.setFont(font());
    const QString line = m_text.isEmpty() ? m_label : m_label + QStringLiteral(": ") + m_text;
    p.drawText(QRect(8 + r + 8, 0, width() - r - 16, height()), Qt::AlignVCenter | Qt::AlignLeft, line);
}
