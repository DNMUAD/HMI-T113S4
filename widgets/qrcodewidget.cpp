#include "qrcodewidget.h"
#include "qrcode.h"

#include <QPainter>
#include <QPaintEvent>

namespace {
const int kQuietZone = 4;
}

QrCodeWidget::QrCodeWidget(QWidget *parent)
    : QWidget(parent)
    , m_dark(QColor(0x11, 0x11, 0x11))
    , m_light(QColor(0xFF, 0xFF, 0xFF))
{
    setMinimumSize(160, 160);
}

void QrCodeWidget::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    rebuild();
    update();
}

QString QrCodeWidget::text() const
{
    return m_text;
}

void QrCodeWidget::setDarkColor(const QColor &color)
{
    m_dark = color;
    rebuild();
    update();
}

void QrCodeWidget::setLightColor(const QColor &color)
{
    m_light = color;
    rebuild();
    update();
}

QSize QrCodeWidget::sizeHint() const
{
    return QSize(220, 220);
}

QSize QrCodeWidget::minimumSizeHint() const
{
    return QSize(160, 160);
}

void QrCodeWidget::rebuild()
{
    m_image = QImage();

    if (m_text.isEmpty())
        return;

    QrCode code;
    const QByteArray payload = m_text.toUtf8();
    if (!code.encode(std::string(payload.constData(), static_cast<size_t>(payload.size()))))
        return;                       // 内容过长：保持空图，由页面给出文字地址

    const int modules = code.size();
    const int total = modules + kQuietZone * 2;

    QImage image(total, total, QImage::Format_RGB32);
    image.fill(m_light);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_dark);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (code.module(x, y))
                painter.drawRect(x + kQuietZone, y + kQuietZone, 1, 1);
        }
    }
    painter.end();

    m_image = image;
}

QImage QrCodeWidget::scaledImage() const
{
    if (m_image.isNull())
        return QImage();

    const int side = qMin(width(), height());
    const int pixels = m_image.width();
    int scale = side / pixels;
    if (scale < 1)
        scale = 1;

    return m_image.scaled(pixels * scale, pixels * scale,
                          Qt::KeepAspectRatio, Qt::FastTransformation);
}

void QrCodeWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));

    if (m_image.isNull()) {
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawText(rect(), Qt::AlignCenter, tr("暂无地址"));
        return;
    }

    const QImage image = scaledImage();
    const QRect target((width() - image.width()) / 2,
                       (height() - image.height()) / 2,
                       image.width(), image.height());
    painter.drawImage(target, image);
}
