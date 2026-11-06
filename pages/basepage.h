#ifndef BASEPAGE_H
#define BASEPAGE_H

#include <QWidget>
#include <functional>

class QLabel;
class QVBoxLayout;
class QGridLayout;
class QScrollArea;
class CardButton;

// 所有功能页的统一基类：标题 + 常驻提示 + 状态行 + 可滚动内容区。
// 统一 LogPanel 由 MainWindow 持有，页面不各自建日志控件。
class BasePage : public QWidget
{
    Q_OBJECT
public:
    explicit BasePage(const QString &title, QWidget *parent = nullptr);

    QString title() const;

    // 模板方法（Template Method）：父类定死「骨架 → 子类内容」的次序，
    // 由 PageFactory 在构造后调用一次；子类只覆写 buildContent()，不再在构造函数里
    // 直接铺控件（避免构造期调用虚函数不生效的坑）。
    void initialize();
    bool isInitialized() const;

    // 生命周期钩子：M6 页面按需加载 / 暂停轮询时在此挂接
    virtual void onEnter();
    virtual void onLeave();

    void setHint(const QString &text);      // 如「跳线帽：左 RS485 / 右 以太网」
    void setStatus(const QString &text);

    void addWidget(QWidget *widget);
    QGridLayout *addGrid();                       // 两列按钮排版用
    CardButton *newButton(const QString &text, const std::function<void()> &fn,
                          const QString &description = QString(), bool accent = false);
    CardButton *addButton(const QString &text, const std::function<void()> &fn,
                          const QString &description = QString(), bool accent = false);

protected:
    // 子类在此放自己的控件与信号绑定（只会被调用一次）
    virtual void buildContent();

    QLabel     *m_status;
    QVBoxLayout *m_body;

private:
    QString     m_title;
    QLabel      *m_hint;
    QScrollArea *m_scroll;
    bool         m_initialized;
};

#endif // BASEPAGE_H
