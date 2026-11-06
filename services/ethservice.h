#ifndef ETHSERVICE_H
#define ETHSERVICE_H

#include "deviceservice.h"

#include <QString>

class CommandSequence;

// eth0：ifconfig up → udhcpc → ping。sysfs 的 operstate/speed 走文件读取，不启进程。
class EthService : public DeviceService
{
    Q_OBJECT
public:
    static EthService *instance();

    void refresh() override;   // ip -4 addr show eth0 + sysfs
    void bringUp();            // ifconfig eth0 up
    void dhcp();               // udhcpc -i eth0
    void pingTest();           // ping -c4 www.baidu.com

    // 组合命令：网卡 UP → DHCP → Ping 验证，任一步失败即中断
    CommandSequence *makeConnectSequence();

signals:
    void linkInfoChanged(const QString &operstate, const QString &speed);
    void pingResult(const QString &raw);

private slots:
    void onFinished(int jobId, int exitCode, int exitStatus, const QString &out);

private:
    explicit EthService(QObject *parent = nullptr);
    static QString readSysfs(const QString &path);
    void readLinkInfo();

    int m_statusJob;
    int m_pingJob;
    int m_actionJob;
};

#endif // ETHSERVICE_H
