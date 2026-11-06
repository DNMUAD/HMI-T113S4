#ifndef STATUSDOT_H
#define STATUSDOT_H

#include <QWidget>

// 状态栏指示灯：圆点 + 文字，状态栏复用（WiFi / ETH / ADB）。
class StatusDot : public QWidget
{
    Q_OBJECT
    Q_ENUM(Level)

public:
    enum Level { Off, On, Busy, Warn, Error };

    explicit StatusDot(const QString &label, QWidget *parent = nullptr);

    void setLevel(Level level);
    Level level() const;
    void setText(const QString &text);
    QString text() const;

    QSize sizeHint() const override;

signals:
    void stateChanged(int level);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_label;
    QString m_text;
    Level   m_level;
};

#endif // STATUSDOT_H
