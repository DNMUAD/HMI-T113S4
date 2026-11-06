#ifndef CARDBUTTON_H
#define CARDBUTTON_H

#include <QPushButton>

// 触摸友好卡片按钮：高度 ≥56px、标题 16–20pt、可带一行说明。
class CardButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QString description READ description WRITE setDescription)

public:
    explicit CardButton(const QString &text, const QString &description = QString(),
                        QWidget *parent = nullptr);

    QString description() const;
    void setDescription(const QString &text);
    void setAccent(bool accent);       // 主操作高亮

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_description;
    bool    m_accent;
};

#endif // CARDBUTTON_H
