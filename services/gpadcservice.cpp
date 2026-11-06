#include "gpadcservice.h"
#include "logpanel.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QFile>
#include <QRegularExpression>
#include <QSocketNotifier>

#ifdef Q_OS_LINUX
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#endif

GpadcService *GpadcService::instance()
{
    return hmi::singleton<GpadcService>([]() { return new GpadcService; });
}

GpadcService::GpadcService(QObject *parent)
    : DeviceService(QStringLiteral("gpadc"), QObject::tr("GPADC"), 0, parent)
    , m_fd(-1)
    , m_notifier(nullptr)
{
}

QString GpadcService::keyName(quint16 code)
{
    switch (code) {
    case 115: return QStringLiteral("音量+");
    case 114: return QStringLiteral("音量-");
    case 119: return QStringLiteral("暂停");
    case 373: return QStringLiteral("播放");
    case 28:  return QStringLiteral("确认");
    default:  return QStringLiteral("未知键 %1").arg(code);
    }
}

QString GpadcService::findEventNode() const
{
    QFile f(QStringLiteral("/proc/bus/input/devices"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LogPanel::post(QStringLiteral("无法读取 /proc/bus/input/devices"), LogPanel::Error);
        return QString();
    }

    QString block;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            if (block.contains(QStringLiteral("Name=\"gpadc\""), Qt::CaseInsensitive)) {
                QRegularExpression rx(QStringLiteral("\\b(event\\d+)\\b"));
                const QRegularExpressionMatch m = rx.match(block);
                if (m.hasMatch())
                    return QStringLiteral("/dev/input/") + m.captured(1);
            }
            block.clear();
        } else {
            block += line + QLatin1Char('\n');
        }
    }
    return QString();
}

bool GpadcService::start()
{
    if (isRunning())
        return true;

    m_eventNode = findEventNode();
    emit deviceChanged(m_eventNode);
    if (m_eventNode.isEmpty()) {
        LogPanel::post(QStringLiteral("未找到 gpadc 输入设备"), LogPanel::Warn);
        publish(DeviceError, tr("未找到 gpadc 设备"));
        return false;
    }

#ifdef Q_OS_LINUX
    m_fd = ::open(qPrintable(m_eventNode), O_RDONLY | O_NONBLOCK);
    if (m_fd < 0) {
        LogPanel::post(QStringLiteral("打开 %1 失败（需 root）").arg(m_eventNode), LogPanel::Error);
        publish(DeviceError, tr("打开 %1 失败").arg(m_eventNode));
        return false;
    }
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this](int) { onActivated(); });
    m_notifier->setEnabled(true);
    LogPanel::post(QStringLiteral("GPADC 监听 %1，事件结构 %2 字节").arg(m_eventNode).arg(sizeof(struct input_event)),
                   LogPanel::Ok);
    publish(DeviceReady, m_eventNode);
    return true;
#else
    LogPanel::post(QStringLiteral("GPADC 事件读取仅支持目标板 Linux（桌面 Kit 跳过）"), LogPanel::Warn);
    publish(DeviceOff, tr("桌面环境不支持"));
    return false;
#endif
}

void GpadcService::stop()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
#ifdef Q_OS_LINUX
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    publish(DeviceOff, tr("已停止监听"));
}

bool GpadcService::isRunning() const
{
    return m_notifier != nullptr;
}

void GpadcService::onActivated()
{
#ifdef Q_OS_LINUX
    struct input_event ev;
    // 用 sizeof(struct input_event) 而非硬编码：32 位 ARM 为 16B，64 位主机为 24B。
    while (::read(m_fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        if (ev.type != EV_KEY)
            continue;
        const QString name = keyName(ev.code);
        LogPanel::post(QStringLiteral("GPADC 按键 %1 (code=%2 value=%3)").arg(name).arg(ev.code).arg(ev.value),
                       LogPanel::Ok);
        emit keyEvent(ev.code, name, ev.value);
    }
#endif
}

HMI_REGISTER_SERVICE("gpadc", GpadcService)
