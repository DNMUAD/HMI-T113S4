#ifndef I2CSERVICE_H
#define I2CSERVICE_H

#include "deviceservice.h"

#include <QString>
#include <QVector>

// /dev/i2c-2。M1 提供只读扫描（不写任何寄存器，避免打扰触摸 0x5d），
// 指定地址读寄存器留到 M3。
class I2cService : public DeviceService
{
    Q_OBJECT
public:
    static I2cService *instance();

    void scan();                                   // 0x03–0x77，I2C_RDWR + I2C_M_RD
    void readRegister(int address, int reg);       // TODO(M3)
    QVector<int> lastScan() const;

    static QString busDevice() { return QStringLiteral("/dev/i2c-2"); }

signals:
    void scanFinished(const QVector<int> &addresses);
    void registerValue(int address, int reg, int value);

private:
    explicit I2cService(QObject *parent = nullptr);
    QVector<int> m_lastScan;
};

#endif // I2CSERVICE_H
