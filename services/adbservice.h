#ifndef ADBSERVICE_H
#define ADBSERVICE_H

#include "deviceservice.h"

#include <QString>

// ADB：/etc/init.d/S50adb_start start|stop，状态查 pidof adbd + /sys/class/udc。
class AdbService : public DeviceService
{
    Q_OBJECT
public:
    static AdbService *instance();

    void refresh() override;
    void start();
    void stop();

private slots:
    void onFinished(int jobId, int exitCode, int exitStatus, const QString &out);

private:
    explicit AdbService(QObject *parent = nullptr);
    int m_statusJob;
    int m_actionJob;
};

#endif // ADBSERVICE_H
