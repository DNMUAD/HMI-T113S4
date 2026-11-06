#include "rs485service.h"
#include "logpanel.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QSerialPort>
#include <QSerialPortInfo>

Rs485Service *Rs485Service::instance()
{
    return hmi::singleton<Rs485Service>([]() { return new Rs485Service; });
}

Rs485Service::Rs485Service(QObject *parent)
    : DeviceService(QStringLiteral("rs485"), QObject::tr("RS485"), 0, parent)
    , m_port(new QSerialPort(this))
{
    connect(m_port, &QSerialPort::readyRead, this, [this]() {
        emit frameReceived(m_port->readAll());
    });
    // M3: 可接 QSerialPort::errorOccurred 细化线缆/权限错误，当前由 LogPanel 统一记录。
}

QStringList Rs485Service::baudRates()
{
    return QStringList() << QStringLiteral("9600") << QStringLiteral("19200")
                         << QStringLiteral("38400") << QStringLiteral("115200");
}

QStringList Rs485Service::availablePorts()
{
    QStringList ports;
    const QList<QSerialPortInfo> infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos)
        ports << info.systemLocation();
    if (!ports.contains(QStringLiteral("/dev/ttyS2")))
        ports.prepend(QStringLiteral("/dev/ttyS2"));   // 目标板默认
    return ports;
}

bool Rs485Service::open(const QString &port, int baudRate)
{
    if (m_port->isOpen())
        m_port->close();

    m_port->setPortName(port);
    m_port->setBaudRate(baudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    const bool ok = m_port->open(QIODevice::ReadWrite);
    LogPanel::post(QStringLiteral("RS485 %1 @ %2 %3")
                   .arg(port).arg(baudRate)
                   .arg(ok ? QStringLiteral("打开成功") : m_port->errorString()),
                   ok ? LogPanel::Ok : LogPanel::Error);
    publish(ok ? DeviceReady : DeviceError,
            ok ? tr("%1 @ %2 bps").arg(port).arg(baudRate) : m_port->errorString());
    return ok;
}

void Rs485Service::close()
{
    if (m_port->isOpen())
        m_port->close();
    publish(DeviceOff, tr("已关闭"));
}

void Rs485Service::send(const QByteArray &data)
{
    if (!m_port->isOpen()) {
        LogPanel::post(QStringLiteral("RS485 未打开，发送取消"), LogPanel::Warn);
        return;
    }
    const qint64 n = m_port->write(data);
    if (n < 0)
        LogPanel::post(QStringLiteral("RS485 发送失败: %1").arg(m_port->errorString()), LogPanel::Error);
    else
        LogPanel::post(QStringLiteral("RS485 TX %1 字节").arg(n), LogPanel::Ok);
}

bool Rs485Service::isOpen() const
{
    return m_port->isOpen();
}

HMI_REGISTER_SERVICE("rs485", Rs485Service)
