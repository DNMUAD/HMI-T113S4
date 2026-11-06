#ifndef GPADCSERVICE_H
#define GPADCSERVICE_H

#include "deviceservice.h"

#include <QString>

class QSocketNotifier;

// GPADC 按键：内核 CONFIG_SUNXI_GPADC=y 已开，无需改内核。
// 扫 /proc/bus/input/devices 找 Name="gpadc" 的 Handlers=eventN，
// QSocketNotifier 读 struct input_event，映射 115/114/119/373/28。
class GpadcService : public DeviceService
{
    Q_OBJECT
public:
    static GpadcService *instance();

    QString findEventNode() const;                 // 返回 /dev/input/eventN 或空
    bool start();
    void stop();
    bool isRunning() const;

    static QString keyName(quint16 code);          // 音量± / 暂停 / 播放 / 确认

signals:
    void keyEvent(int code, const QString &name, int value);
    void deviceChanged(const QString &eventNode);

private:
    explicit GpadcService(QObject *parent = nullptr);
    void onActivated();

    int              m_fd;
    QSocketNotifier *m_notifier;
    QString          m_eventNode;
};

#endif // GPADCSERVICE_H
