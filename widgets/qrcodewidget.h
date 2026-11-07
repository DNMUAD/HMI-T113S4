#ifndef HMI_WIDGETS_QRCODEWIDGET_H
#define HMI_WIDGETS_QRCODEWIDGET_H

#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>

// 把 core/qrcode 的模块矩阵画到屏幕上（含 4 模块静区）
//
// 只需要一个 setText()：内容变化时重建位图，尺寸变化时按整数倍缩放，
// 保证模块边缘不被插值糊掉 —— 手机摄像头对模糊边缘很敏感。
class QrCodeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QrCodeWidget(QWidget *parent = nullptr);

    void setText(const QString &text);
    QString text() const;

    void setDarkColor(const QColor &color);
    void setLightColor(const QColor &color);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuild();
    QImage scaledImage() const;

    QString m_text;
    QColor  m_dark;
    QColor  m_light;
    QImage  m_image;        // 1 像素 = 1 模块，四周含 4 模块静区
};

#endif // HMI_WIDGETS_QRCODEWIDGET_H
