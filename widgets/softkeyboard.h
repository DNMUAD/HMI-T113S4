#ifndef HMI_WIDGETS_SOFTKEYBOARD_H
#define HMI_WIDGETS_SOFTKEYBOARD_H

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

class QPushButton;
class QStackedWidget;
class QHBoxLayout;
class QVBoxLayout;

// 纯 QWidget 软键盘（不依赖 QML / Quick）。
//
// 为什么自己写：板端 rootfs 里 libQt5VirtualKeyboard.so 根本没装进 target
// （只装在 staging，buildroot 的 qt5virtualkeyboard.mk 漏了拷库），
// 所以 S70launcher 里那句 QT_IM_MODULE=qtvirtualkeyboard 不生效，
// 点输入框没有任何反应。补库这条路还要引入 Quick + scenegraph，
// 而板端平台插件只有 linuxfb（单窗口）、连 libEGL/libGL 都没有，
// 为了一个「输 WiFi 密码」的需求代价太大。
//
// 设计要点（参考 QOnScreenKeyboard / 各类嵌入式 OSK 的常见做法）：
//  · 作为主窗口的子控件停靠在底部，**不创建新的顶层窗口** —— linuxfb 下最稳；
//  · 自身不认识具体页面：装一个 QApplication 级事件过滤器，
//    任何可编辑控件被点中/获得焦点就弹出，焦点转到非输入控件就收起；
//  · 按键全部 Qt::NoFocus，点键时输入框不失焦、光标不闪断；
//  · 三张键面（小写/大写/数字符号）预建好放进 QStackedWidget，
//    切大小写只是切页 —— 不做「删掉旧按钮重建」，
//    否则在按钮自己的 clicked 回调里 delete 自己会崩。
class SoftKeyboard : public QWidget
{
    Q_OBJECT
public:
    explicit SoftKeyboard(QWidget *parent = nullptr);

    // 由 MainWindow 在界面搭好后调一次：开始监视全应用的可编辑控件
    void attachGlobalFilter();

    void attachTo(QWidget *target);     // 对指定输入框弹出
    void dismiss();                     // 收起（保留目标，便于再次点击直接弹出）
    bool isAttached() const;

    void reposition();                  // 宿主尺寸变化时重新停靠到底部

signals:
    void shownFor(QWidget *target);
    void dismissed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    enum Page
    {
        PageLower = 0,
        PageUpper,
        PageDigits
    };

    QWidget *buildLettersPage(bool upper);
    QWidget *buildDigitsPage();
    QHBoxLayout *buildBottomRow(QWidget *page);   // 三页共用的底行
    void     syncContextKeys();         // 「显示/隐藏」键随目标是否为密码框切换
    void     insertText(const QString &text);
    void     doBackspace();
    void     doClear();
    void     doContext();
    void     finish();

    bool targetIsLineEdit() const;
    void ensureTargetVisible();

    static bool isTextInput(QWidget *w);
    static bool isInsideKeyboard(const QWidget *w, const QWidget *keyboard);

    QPointer<QWidget>   m_target;
    QStackedWidget     *m_pages;
    QList<QPushButton *> m_revealKeys;   // 三张页各一个「显示/隐藏」键
    bool                m_filterInstalled;
    // 「当前目标是密码框」这件事必须单独记：不能用 echoMode 反推 ——
    // 用户点过「显示」后 echoMode 变成 Normal，反推会以为它不是密码框，
    // 于是那个开关键自己消失，再也没法切回密码态。
    bool                m_pwField;
};

#endif // HMI_WIDGETS_SOFTKEYBOARD_H
