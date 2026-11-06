#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class BaseView;
class QLabel;
class QStackedWidget;
class QTimer;

// 主窗口：标题栏 + 页面容器 + 状态栏
// 新增页面只需继承 BaseView 并调用 addView()
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void addView(BaseView *view);
    int  currentIndex() const;

public slots:
    void setCurrentIndex(int index);

signals:
    void currentViewChanged(int index);

private slots:
    void updateClock();

private:
    void buildUi();
    QWidget *createTitleBar();
    QWidget *createStatusBar();

    QStackedWidget *m_stack;
    QLabel         *m_titleLabel;
    QLabel         *m_clockLabel;
    QLabel         *m_stateLabel;
    QLabel         *m_versionLabel;
    QTimer         *m_clockTimer;
    int             m_currentIndex;
};
#endif // MAINWINDOW_H
