#include "cardbutton.h"
#include "uimetrics.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPen>

CardButton::CardButton(const QString &text, const QString &description, QWidget *parent)
    : QPushButton(text, parent)
    , m_description(description)
    , m_accent(false)
    , m_navMode(false)
    , m_highlighted(false)
{
    setObjectName(QStringLiteral("cardButton"));
    setMinimumHeight(m_description.isEmpty() ? UiMetrics::ButtonHeight
                                             : UiMetrics::ButtonHeightTwoLine);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // 触摸屏不需要键盘焦点框；点完不留焦点高亮，避免和左侧导航的选中态打架
    setFocusPolicy(Qt::NoFocus);
}

QString CardButton::description() const
{
    return m_description;
}

void CardButton::setDescription(const QString &text)
{
    m_description = text;
    setMinimumHeight(m_description.isEmpty() ? UiMetrics::ButtonHeight
                                             : UiMetrics::ButtonHeightTwoLine);
    updateGeometry();
    update();
}

void CardButton::setAccent(bool accent)
{
    m_accent = accent;
    update();
}

void CardButton::setNavMode(bool navMode)
{
    m_navMode = navMode;
    update();
}

void CardButton::setHighlighted(bool highlighted)
{
    if (m_highlighted == highlighted)
        return;
    m_highlighted = highlighted;
    update();
}

QSize CardButton::sizeHint() const
{
    return QSize(200, m_description.isEmpty() ? UiMetrics::ButtonHeight
                                              : UiMetrics::ButtonHeightTwoLine);
}

QSize CardButton::minimumSizeHint() const
{
    return QSize(UiMetrics::ButtonMinWidth,
                 m_description.isEmpty() ? UiMetrics::ButtonHeight
                                         : UiMetrics::ButtonHeightTwoLine);
}

void CardButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 高亮 = 主操作按钮 或 导航栏里的当前项；两者不能同时为真，
    // 避免用户看到两个蓝色控件而误以为焦点重复。
    const bool highlight = m_accent || m_highlighted;

    QColor bg, border, titleColor;
    if (highlight) {
        bg = QColor(QStringLiteral("#4187F0"));
        border = QColor(QStringLiteral("#2F6FD0"));
        titleColor = QColor(QStringLiteral("#FFFFFF"));
    } else if (m_navMode) {
        // 未选中的导航项与导航栏底色一致，只有选中才浮出来
        bg = QColor(QStringLiteral("#2F3A4A"));
        border = QColor(QStringLiteral("#2F3A4A"));
        titleColor = QColor(QStringLiteral("#DCE6F5"));
    } else {
        bg = QColor(QStringLiteral("#FFFFFF"));
        border = QColor(QStringLiteral("#C8D5EA"));
        titleColor = QColor(QStringLiteral("#2F3A4A"));
    }

    if (isDown())
        bg = highlight ? QColor(QStringLiteral("#2559A8"))
                       : (m_navMode ? QColor(QStringLiteral("#46566D"))
                                    : QColor(QStringLiteral("#DCE8FA")));
    else if (!isEnabled())
        bg = QColor(QStringLiteral("#F0F0F0"));
    else if (underMouse() && !highlight)
        bg = m_navMode ? QColor(QStringLiteral("#46566D")) : QColor(QStringLiteral("#EEF4FF"));

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(border, 1));
    p.setBrush(bg);
    p.drawRoundedRect(box, 6, 6);

    const int padX = 14;
    const QRect area = rect().adjusted(padX, 3, -padX, -3);
    if (area.width() <= 0 || area.height() <= 0)
        return;

    QFont titleFont = font();
    titleFont.setPixelSize(UiMetrics::TextPixelSize);
    titleFont.setBold(true);
    const QFontMetrics tm(titleFont);

    if (m_description.isEmpty()) {
        p.setFont(titleFont);
        p.setPen(titleColor);
        p.drawText(area, Qt::AlignVCenter | Qt::AlignLeft,
                   tm.elidedText(text(), Qt::ElideRight, area.width()));
        return;
    }

    QFont descFont = font();
    descFont.setPixelSize(UiMetrics::DescPixelSize);
    const QFontMetrics dm(descFont);

    // 两行按真实行高居中排布，不再用 height()/2 硬切，避免不同字号下文字互相压边
    const int gap = 2;
    const int total = tm.height() + gap + dm.height();
    const int top = area.top() + (area.height() - total) / 2;

    p.setFont(titleFont);
    p.setPen(titleColor);
    p.drawText(QRect(area.left(), top, area.width(), tm.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               tm.elidedText(text(), Qt::ElideRight, area.width()));

    p.setFont(descFont);
    if (highlight)
        p.setPen(QColor(QStringLiteral("#DCE8FF")));
    else if (m_navMode)
        p.setPen(QColor(QStringLiteral("#9FB0C6")));
    else
        p.setPen(QColor(QStringLiteral("#6B778C")));
    p.drawText(QRect(area.left(), top + tm.height() + gap, area.width(), dm.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               dm.elidedText(m_description, Qt::ElideRight, area.width()));
}
