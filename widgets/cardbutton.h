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
    void setAccent(bool accent);       // 主操作高亮（页面里的主按钮）
    // 导航栏用：未选中时融入深色底，选中时高亮。与 accent 分开，
    // 避免「总览按钮常驻蓝色 + 列表项选中蓝」两个控件同时像被选中。
    void setNavMode(bool navMode);
    void setHighlighted(bool highlighted);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_description;
    bool    m_accent;
    bool    m_navMode;
    bool    m_highlighted;
};

#endif // CARDBUTTON_H
