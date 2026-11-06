#ifndef RS485SERVICE_H
#define RS485SERVICE_H

#include "deviceservice.h"

#include <QByteArray>
#include <QStringList>

class QSerialPort;

// /dev/ttyS2。半双工方向由驱动 sunxi,uart-485oe 管，应用层不切 DE/RE。
// 注意：SDK 文档里的 /dev/ttyRS485 未经验证，板端先用 ls /dev/ttyS* + dmesg | grep ttyS 核对。
class Rs485Service : public DeviceService
{
    Q_OBJECT
public:
    static Rs485Service *instance();

    static QStringList baudRates();        // 9600 / 19200 / 38400 / 115200
    static QStringList availablePorts();

    bool open(const QString &port, int baudRate);
    void close();
    void send(const QByteArray &data);
    bool isOpen() const;

signals:
    void frameReceived(const QByteArray &data);

private:
    explicit Rs485Service(QObject *parent = nullptr);
    QSerialPort *m_port;
};

#endif // RS485SERVICE_H
