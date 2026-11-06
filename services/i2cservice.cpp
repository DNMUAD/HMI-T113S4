#include "i2cservice.h"
#include "logpanel.h"
#include "serviceregistry.h"
#include "singleton.h"

#ifdef Q_OS_LINUX
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#endif

I2cService *I2cService::instance()
{
    return hmi::singleton<I2cService>([]() { return new I2cService; });
}

I2cService::I2cService(QObject *parent)
    : DeviceService(QStringLiteral("i2c"), QObject::tr("I2C"), 0, parent)
{
}

QVector<int> I2cService::lastScan() const
{
    return m_lastScan;
}

void I2cService::scan()
{
    m_lastScan.clear();
    publish(DeviceBusy, tr("扫描 0x03–0x77"));
#ifdef Q_OS_LINUX
    const int fd = ::open("/dev/i2c-2", O_RDWR);
    if (fd < 0) {
        LogPanel::post(QStringLiteral("打开 /dev/i2c-2 失败（需 root + 内核 i2c-dev）"), LogPanel::Error);
        publish(DeviceError, tr("/dev/i2c-2 打开失败"));
        emit scanFinished(m_lastScan);
        return;
    }

    for (int addr = 0x03; addr <= 0x77; ++addr) {
        __u8 byte = 0;
        struct i2c_msg msg;
        msg.addr  = static_cast<__u16>(addr);
        msg.flags = I2C_M_RD;          // 只读探测，绝不写
        msg.len   = 1;
        msg.buf   = &byte;
        struct i2c_rdwr_ioctl_data data;
        data.msgs  = &msg;
        data.nmsgs = 1;
        if (::ioctl(fd, I2C_RDWR, &data) >= 0)
            m_lastScan.append(addr);
    }
    ::close(fd);

    QStringList hex;
    for (int a : m_lastScan)
        hex << QStringLiteral("0x%1").arg(a, 2, 16, QLatin1Char('0'));
    LogPanel::post(QStringLiteral("I2C-2 扫描完成，应答: %1")
                   .arg(hex.isEmpty() ? QStringLiteral("(无)") : hex.join(QStringLiteral(" "))), LogPanel::Ok);
    publish(m_lastScan.isEmpty() ? DeviceOff : DeviceReady,
            m_lastScan.isEmpty() ? tr("无应答设备")
                                 : tr("%1 个设备").arg(m_lastScan.size()));
#else
    LogPanel::post(QStringLiteral("I2C 扫描仅在目标板 Linux 上可用（桌面 Kit 跳过）"), LogPanel::Warn);
    publish(DeviceOff, tr("桌面环境不支持"));
#endif
    // TODO(M3): 单次全地址扫描仍有毫秒级同步等待，接入页面后移入工作线程。
    emit scanFinished(m_lastScan);
}

void I2cService::readRegister(int address, int reg)
{
    // TODO(M3): I2C_RDWR 先写寄存器地址再读；默认不主动写，避免打扰触摸 0x5d。
    LogPanel::post(QStringLiteral("读寄存器接口预留: addr=0x%1 reg=0x%2（待 M3）")
                   .arg(address, 2, 16, QLatin1Char('0')).arg(reg, 2, 16, QLatin1Char('0')),
                   LogPanel::Warn);
}

HMI_REGISTER_SERVICE("i2c", I2cService)
