#include "gpadcservice.h"
#include "logpanel.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QSocketNotifier>

#ifdef Q_OS_LINUX
#include <linux/input.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

// 内置动作 id。页面下拉框把「无动作 / 仅回显」与本模块外部注册的动作拼在一起。
const char *kActionNone = "none";
const char *kActionEcho = "echo";

// 绑定持久化文件。
//
// 为什么不写进 /etc/hmi.ini：那是一个带大量注释的样例文件，QSettings 保存时会
// 整份重写成纯键值对，注释全丢——所以绑定单独一个文件，互不干扰。
QString bindingStorePath()
{
#ifdef HMI_DEVICE_TARGET
    return QStringLiteral("/etc/hmi-gpadc.ini");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/hmi-gpadc.ini");
#endif
}

// 从 /proc/bus/input/devices 的一段里取 eventN。
// 只在 "H: Handlers=" 行里找：同一段里 Sysfs= 也有 "input3" 之类的字样，
// 全段正则容易抓错；Handlers 才是设备节点真名（kbd event2 / event1 ...）。
QString eventFromBlock(const QString &block)
{
    const QStringList lines = block.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.trimmed().startsWith(QStringLiteral("H:"), Qt::CaseInsensitive))
            continue;
        static const QRegularExpression rx(QStringLiteral("\\b(event\\d+)\\b"));
        const QRegularExpressionMatch m = rx.match(line);
        if (m.hasMatch())
            return QStringLiteral("/dev/input/") + m.captured(1);
    }
    return QString();
}

QString nameFromBlock(const QString &block)
{
    const QStringList lines = block.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.trimmed().startsWith(QStringLiteral("N:"), Qt::CaseInsensitive))
            continue;
        static const QRegularExpression rx(QStringLiteral("Name=\"([^\"]*)\""));
        const QRegularExpressionMatch m = rx.match(line);
        if (m.hasMatch())
            return m.captured(1);
    }
    return QString();
}

// 把 /proc/bus/input/devices 切成一段段（空行分隔）。
// 返回 false 表示文件根本打不开；true 但列表为空表示「能读、里面没有设备」
// ——这两种情况在板端含义完全不同（后者常见于桌面 Kit 的 /proc 虚拟化）。
bool inputDeviceBlocks(QStringList *blocks)
{
    blocks->clear();
    QFile f(QStringLiteral("/proc/bus/input/devices"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString block;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            if (!block.isEmpty())
                *blocks << block;
            block.clear();
        } else {
            block += line + QLatin1Char('\n');
        }
    }
    if (!block.isEmpty())
        *blocks << block;
    return true;
}

} // namespace

GpadcService *GpadcService::instance()
{
    return hmi::singleton<GpadcService>([]() { return new GpadcService; });
}

GpadcService::GpadcService(QObject *parent)
    : DeviceService(QStringLiteral("gpadc"), QObject::tr("GPADC"), 0, parent)
    , m_fd(-1)
    , m_notifier(nullptr)
{
    // 内置动作先入表，保证下拉框永远至少有「无动作 / 仅回显」两项
    addAction(QString::fromLatin1(kActionNone), QObject::tr("无动作"),
              QObject::tr("只写 Debug 日志，不触发任何业务"));
    addAction(QString::fromLatin1(kActionEcho), QObject::tr("仅回显"),
              QObject::tr("进日志 + 页面计数，不触发任何业务"));

    resetBindings();

    // 读回上次选择的绑定；文件不存在就全用默认（仅回显）
    QSettings store(bindingStorePath(), QSettings::IniFormat);
    const QList<KeyInfo> all = keys();
    for (const KeyInfo &k : all) {
        const QString saved = store.value(QStringLiteral("bindings/%1").arg(k.name)).toString().trimmed();
        if (!saved.isEmpty())
            m_bindings.insert(k.code, saved);
    }
}

QList<GpadcService::KeyInfo> GpadcService::keys()
{
    QList<KeyInfo> list;
    // 键码与 board.dts 的 key0_val / key2_val / key3_val 一一对应。
    // 三个键在原理图上分别接不同分压电阻，按下的电压不同，驱动据 keyN_vol 判区间。
    list << KeyInfo{ KeyK1, QStringLiteral("K1"), QObject::tr("键码 115 · 0x73") };
    list << KeyInfo{ KeyK2, QStringLiteral("K2"), QObject::tr("键码 119 · 0x77") };
    list << KeyInfo{ KeyK3, QStringLiteral("K3"), QObject::tr("键码 373 · 0x175") };
    return list;
}

QString GpadcService::keyName(int code)
{
    const QList<KeyInfo> all = keys();
    for (const KeyInfo &k : all) {
        if (k.code == code)
            return k.name;
    }
    return QObject::tr("键 %1").arg(codeText(code));
}

QString GpadcService::codeText(int code)
{
    return QStringLiteral("0x%1 (%2)")
        .arg(code, 2, 16, QLatin1Char('0'))
        .arg(code);
}

// ---------------------------------------------------------------- 动作表

QList<GpadcService::Action> GpadcService::actions() const
{
    return m_actions;
}

void GpadcService::addAction(const QString &id, const QString &name, const QString &description,
                             ActionHandler handler)
{
    if (id.trimmed().isEmpty())
        return;

    // 同 id 视为覆盖：允许模块在运行中替换自己的实现，但不产生重复下拉项
    for (int i = 0; i < m_actions.size(); ++i) {
        if (m_actions.at(i).id == id) {
            m_actions[i].name = name;
            m_actions[i].description = description;
            m_handlers.insert(id, handler);
            return;
        }
    }

    Action a;
    a.id = id;
    a.name = name;
    a.description = description;
    m_actions << a;
    m_handlers.insert(id, handler);
}

QString GpadcService::actionName(const QString &id) const
{
    for (const Action &a : m_actions) {
        if (a.id == id)
            return a.name;
    }
    return id;
}

// ---------------------------------------------------------------- 绑定表

QString GpadcService::binding(int code) const
{
    return m_bindings.value(code, QString::fromLatin1(kActionEcho));
}

void GpadcService::setBinding(int code, const QString &actionId)
{
    if (actionId.trimmed().isEmpty())
        return;

    const QString old = binding(code);
    if (old == actionId)
        return;

    m_bindings.insert(code, actionId);

    QSettings store(bindingStorePath(), QSettings::IniFormat);
    store.setValue(QStringLiteral("bindings/%1").arg(keyName(code)), actionId);
    store.sync();

    LogPanel::post(QStringLiteral("GPADC %1 绑定：%2 → %3")
                   .arg(keyName(code), actionName(old), actionName(actionId)),
                   LogPanel::Info);
}

void GpadcService::resetBindings()
{
    m_bindings.clear();
    const QList<KeyInfo> all = keys();
    for (const KeyInfo &k : all)
        m_bindings.insert(k.code, QString::fromLatin1(kActionEcho));
}

QString GpadcService::bindingSummary() const
{
    QStringList parts;
    const QList<KeyInfo> all = keys();
    for (const KeyInfo &k : all)
        parts << QStringLiteral("%1→%2").arg(k.name, actionName(binding(k.code)));
    return parts.join(QStringLiteral(" / "));
}

// ---------------------------------------------------------------- 计数

int GpadcService::pressCount(int code) const
{
    return m_pressCount.value(code, 0);
}

void GpadcService::resetCounters()
{
    m_pressCount.clear();
}

// ---------------------------------------------------------------- 设备发现

QStringList GpadcService::describeDevices() const
{
    QStringList blocks;
    if (!inputDeviceBlocks(&blocks))
        return QStringList();               // 文件都打不开：由 findEventNode 记错误日志

    QStringList list;
    for (const QString &block : blocks) {
        const QString name = nameFromBlock(block);
        const QString node = eventFromBlock(block);
        if (name.isEmpty() && node.isEmpty())
            continue;
        list << QStringLiteral("%1 → %2")
                    .arg(name.isEmpty() ? tr("(无名)") : name,
                         node.isEmpty() ? tr("无 event 节点") : node);
    }
    return list;
}

// 按「名字含 gpadc」匹配，而不是 Name="gpadc" 精确匹配。
// 驱动（drivers/input/sensor/sunxi_gpadc.c:955）注册的名字是 sunxi-gpadc0，
// 官方文档里写的 gpadc 是驱动的平台设备名（.name = "gpadc"），不是 input 设备名。
// 文档照抄会永远匹配不到，这是本模块第一版的实际故障点。
//
// 这是一个纯查询：不打日志。查不到设备时要不要报错由调用方决定——
// refresh() 每 5 秒可能被「刷新状态栏」带一次，在这里打日志会把日志面板刷满。
QString GpadcService::findEventNode() const
{
    QStringList blocks;
    if (!inputDeviceBlocks(&blocks))
        return QString();

    for (const QString &block : blocks) {
        const QString name = nameFromBlock(block);
        if (!name.contains(QStringLiteral("gpadc"), Qt::CaseInsensitive))
            continue;
        const QString node = eventFromBlock(block);
        if (!node.isEmpty())
            return node;
    }
    return QString();
}

QString GpadcService::eventNode() const
{
    return m_eventNode;
}

// ---------------------------------------------------------------- 生命周期

bool GpadcService::start()
{
    if (isRunning())
        return true;

    m_eventNode = findEventNode();
    emit deviceChanged(m_eventNode);

    if (m_eventNode.isEmpty()) {
        // 两种成因分开说，否则现场只能看到一句「未找到」，没法判断该查内核还是查权限
        QStringList blocks;
        const bool readable = inputDeviceBlocks(&blocks);
        LogPanel::post(readable
                       ? QStringLiteral("未找到 GPADC 输入设备：/proc/bus/input/devices 里没有名字含 "
                                        "gpadc 的条目（内核 CONFIG_SUNXI_GPADC=y？"
                                        "board.dts 的 &gpadc status 是否为 okay？）")
                       : QStringLiteral("未找到 GPADC 输入设备：/proc/bus/input/devices 打不开"
                                        "（需 root；或内核没编 input 子系统）"),
                       LogPanel::Warn);
        publish(DeviceError, readable ? tr("未找到 gpadc 设备")
                                      : tr("读不到 input 设备列表"));
        return false;
    }

#ifdef Q_OS_LINUX
    m_fd = ::open(qPrintable(m_eventNode), O_RDONLY | O_NONBLOCK);
    if (m_fd < 0) {
        LogPanel::post(QStringLiteral("打开 %1 失败：%2（需 root）")
                       .arg(m_eventNode, QString::fromLocal8Bit(::strerror(errno))),
                       LogPanel::Error);
        publish(DeviceError, tr("打开 %1 失败").arg(m_eventNode));
        return false;
    }

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this](int) { onActivated(); });
    m_notifier->setEnabled(true);

    LogPanel::post(QStringLiteral("GPADC 监听 %1（input_event %2 字节）· 绑定 %3")
                   .arg(m_eventNode)
                   .arg(sizeof(struct input_event))
                   .arg(bindingSummary()),
                   LogPanel::Ok);
    publish(DeviceReady, m_eventNode);
    return true;
#else
    LogPanel::post(QStringLiteral("GPADC 事件读取仅支持目标板 Linux（桌面 Kit 跳过）"),
                   LogPanel::Warn);
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
    m_eventNode.clear();
    emit deviceChanged(m_eventNode);
    publish(DeviceOff, tr("已停止监听"));
}

bool GpadcService::isRunning() const
{
    return m_notifier != nullptr;
}

// 注册表 / 「刷新状态栏」会调到这里：只做一次轻量探测，不抢着打开设备。
// 页面上点「开始监听」才真正 open + QSocketNotifier。
void GpadcService::refresh()
{
    if (isRunning())
        return;

    const QString node = findEventNode();
    if (node != m_eventNode) {
        m_eventNode = node;
        emit deviceChanged(m_eventNode);
    }

    if (node.isEmpty())
        publish(DeviceError, tr("未找到 gpadc 设备"));
    else
        publish(DeviceOff, tr("就绪，未监听 · %1").arg(node));
}

// ---------------------------------------------------------------- 事件

// 与 onActivated() 共用 handleCode()：硬件读出来的 ev.code / ev.value
// 和这里传进去的两个参数走的是同一条派发路径，没有「测试分支」。
void GpadcService::simulateKey(int code, int value)
{
    handleCode(code, value);
}

void GpadcService::onActivated()
{
#ifdef Q_OS_LINUX
    struct input_event ev;
    // 用 sizeof(struct input_event) 而非硬编码：32 位 ARM 为 16B，64 位主机为 24B。
    ssize_t n = 0;
    while ((n = ::read(m_fd, &ev, sizeof(ev))) == static_cast<ssize_t>(sizeof(ev))) {
        // 只关心 EV_KEY；EV_SYN / EV_MSC 直接丢（驱动在按键通道上只发 EV_KEY）
        if (ev.type != EV_KEY)
            continue;
        handleCode(ev.code, ev.value);
    }

    // n == 0：设备被拔出或被驱动注销，event 节点失效。
    //
    // 不在这里同步 stop()：onActivated 是 QSocketNotifier 的回调，回调里
    // delete 通知器本身属于「在自己的信号里删自己」——用队列转到事件循环下一轮，
    // 由 stop() 正常关闭 fd 并发布状态。
    if (n == 0) {
        LogPanel::post(QStringLiteral("GPADC 设备已移除（%1）").arg(m_eventNode), LogPanel::Warn);
        QMetaObject::invokeMethod(this, [this]() { stop(); }, Qt::QueuedConnection);
    }
#endif
}

void GpadcService::handleCode(int code, int value)
{
    const QString name = keyName(code);

    switch (value) {
    case 0:                                  // 松开
        LogPanel::post(QStringLiteral("GPADC %1 松开（%2）").arg(name, codeText(code)),
                       LogPanel::Debug);
        emit keyEvent(code, name, value);
        emit keyReleased(name, code);
        break;

    case 1:                                  // 按下
        m_pressCount[code] = m_pressCount.value(code, 0) + 1;
        LogPanel::post(QStringLiteral("GPADC %1 按下（%2）").arg(name, codeText(code)),
                       LogPanel::Ok);
        emit keyEvent(code, name, value);
        emit keyPressed(name, code);
        dispatch(code);
        break;

    default:                                 // 2 = 自动重复（驱动按 EV_REP 注册时才有）
        LogPanel::post(QStringLiteral("GPADC %1 重复（value=%2）").arg(name).arg(value),
                       LogPanel::Debug);
        emit keyEvent(code, name, value);
        break;
    }
}

// 派发「这条键当前绑定的动作」。
//
// 服务本身不认识任何业务：音量、播放、切页……都由外部用 addAction() 注册。
// 这里只做三件事：查绑定 → 查处理器 → 调用，并把结果广播给页面。
void GpadcService::dispatch(int code)
{
    const QString actionId = binding(code);
    const QString name = keyName(code);

    emit actionTriggered(name, code, actionId);

    if (actionId == QString::fromLatin1(kActionNone))
        return;

    if (actionId == QString::fromLatin1(kActionEcho))
        return;                              // 回显由 keyPressed / 计数承担

    const ActionHandler handler = m_handlers.value(actionId);
    if (!handler) {
        // 绑了一个没有处理器的动作（例如外部模块注册后又卸载）：
        // 不静默失败，否则用户会以为按键坏了。
        LogPanel::post(QStringLiteral("GPADC %1 绑定的动作「%2」没有处理器")
                       .arg(name, actionName(actionId)),
                       LogPanel::Warn);
        return;
    }
    handler(name, code);
}

HMI_REGISTER_SERVICE("gpadc", GpadcService)
