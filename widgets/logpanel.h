#ifndef LOGPANEL_H
#define LOGPANEL_H

#include <QWidget>

class QPlainTextEdit;
class QPushButton;

// 全局统一日志面板：只读、限 500 行、跨模块可 post。
// 所有外部命令的 stdout/stderr（MergedChannels）与 qWarning 都汇聚到这里。
class LogPanel : public QWidget
{
    Q_OBJECT

public:
    enum Level { Info, Ok, Warn, Error, Debug };
    Q_ENUM(Level)

    // 唯一实例，须在 GUI 线程首次创建（MainWindow 构造时已创建）。
    static LogPanel *instance();

    // 线程安全入口：任何 service / ProcessRunner 都可调用，投递到 GUI 线程追加。
    static void post(const QString &message, Level level = Info);

    void setMaxLines(int lines);
    int  maxLines() const;

public slots:
    void clear();

private slots:
    void appendLine(const QString &message, int level);

private:
    explicit LogPanel(QWidget *parent = nullptr);

    QPlainTextEdit *m_view;
    QPushButton    *m_clearButton;
};

#endif // LOGPANEL_H
