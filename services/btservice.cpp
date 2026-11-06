#include "btservice.h"
#include "commandspec.h"
#include "logpanel.h"
#include "processrunner.h"
#include "serviceregistry.h"
#include "singleton.h"

BtService *BtService::instance()
{
    return hmi::singleton<BtService>([]() { return new BtService; });
}

BtService::BtService(QObject *parent)
    : DeviceService(QStringLiteral("bt"), QObject::tr("蓝牙"), 15000, parent)
    , m_statusJob(-1)
{
    connect(ProcessRunner::instance(), &ProcessRunner::finished,
            this, &BtService::onFinished);
}

void BtService::refresh()
{
    // 无蓝牙栈的机器上 hciconfig 不存在，属正常探测失败 → quiet 只记 Debug
    m_statusJob = ProcessRunner::instance()->submit(CommandSpec(QStringLiteral("hciconfig"))
                                                    .arg(QStringLiteral("-a"))
                                                    .quiet()
                                                    .describe(tr("查询蓝牙控制器")));
}

void BtService::powerOn()
{
    // TODO(M6): 按 rootfs 实际蓝牙栈（btmgmt / bluetoothctl / hciattach）实现。
    LogPanel::post(QStringLiteral("蓝牙上电接口预留，待 M6 实现"), LogPanel::Warn);
}

void BtService::powerOff()
{
    LogPanel::post(QStringLiteral("蓝牙下电接口预留，待 M6 实现"), LogPanel::Warn);
}

void BtService::scan()
{
    LogPanel::post(QStringLiteral("蓝牙扫描接口预留，待 M6 实现"), LogPanel::Warn);
}

void BtService::onFinished(int jobId, int exitCode, int, const QString &out)
{
    if (jobId != m_statusJob)
        return;
    m_statusJob = -1;
    const bool present = exitCode == 0 && out.contains(QStringLiteral("hci"), Qt::CaseInsensitive);
    publish(present ? DeviceReady : DeviceOff,
            present ? tr("控制器就绪") : tr("未检测到控制器"));
}

HMI_REGISTER_SERVICE("bt", BtService)
