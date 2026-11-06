#ifndef HMI_CORE_COMMANDINVOKER_H
#define HMI_CORE_COMMANDINVOKER_H

#include <QObject>
#include <QQueue>

#include "command.h"

class CommandRunner;

// 设计模式：调用者（Invoker，命令模式的第四个角色）
//
// 页面把命令「交给」它，它负责排队、串行执行、失败汇总、取消 ——
// 于是「同一时刻只允许一组硬件动作在跑」这条规则只实现一次，
// 也顺手解决了连点按钮把 /dev/i2c-2 打爆的问题。
class CommandInvoker : public QObject
{
    Q_OBJECT
public:
    static CommandInvoker *instance();

    void setRunner(CommandRunner *runner);
    CommandRunner *runner() const;

    void enqueue(CommandSequence *sequence);   // 接管所有权；忙则排队
    bool isBusy() const;
    int  queuedCount() const;
    QString activeDescription() const;

public slots:
    void cancelCurrent();                      // 取消当前并清空队列

signals:
    void busyChanged(bool busy);
    void sequenceStarted(const QString &description);
    void sequenceFinished(const QString &description, bool ok, const QString &summary);

private slots:
    void onSequenceFinished(bool ok, const QString &summary);

private:
    explicit CommandInvoker(QObject *parent = nullptr);

    void startNext();

    CommandRunner         *m_runner;
    QQueue<CommandSequence *> m_queue;
    CommandSequence       *m_active;
};

#endif // HMI_CORE_COMMANDINVOKER_H
