#include "adbservice.h"
#include "commandspec.h"
#include "processrunner.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QDir>

AdbService *AdbService::instance()
{
    return hmi::singleton<AdbService>([]() { return new AdbService; });
}

AdbService::AdbService(QObject *parent)
    : DeviceService(QStringLiteral("adb"), QObject::tr("ADB"), 5000, parent)
    , m_statusJob(-1)
    , m_actionJob(-1)
{
    connect(ProcessRunner::instance(), &ProcessRunner::finished,
            this, &AdbService::onFinished);
}

void AdbService::refresh()
{
    // pidof 未命中时返回非零码属正常探测，用 quiet 避免每 5s 刷一条 Error
    m_statusJob = ProcessRunner::instance()->submit(CommandSpec(QStringLiteral("pidof"))
                                                    .arg(QStringLiteral("adbd"))
                                                    .quiet()
                                                    .describe(tr("探测 adbd 进程")));
}

void AdbService::start()
{
    publish(DeviceBusy, tr("启动 adbd"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("sh"),
                                                  QStringList() << QStringLiteral("/etc/init.d/S50adb_start")
                                                                << QStringLiteral("start"));
}

void AdbService::stop()
{
    publish(DeviceBusy, tr("停止 adbd"));
    m_actionJob = ProcessRunner::instance()->run(QStringLiteral("sh"),
                                                  QStringList() << QStringLiteral("/etc/init.d/S50adb_start")
                                                                << QStringLiteral("stop"));
}

void AdbService::onFinished(int jobId, int exitCode, int, const QString &out)
{
    if (jobId == m_statusJob) {
        m_statusJob = -1;
        const bool running = (exitCode == 0 && !out.trimmed().isEmpty());
        const QString udc = QDir(QStringLiteral("/sys/class/udc"))
                                .entryList(QDir::Dirs | QDir::NoDotAndDotDot).join(QStringLiteral(","));
        publish(running ? DeviceReady : DeviceOff,
                running ? (udc.isEmpty() ? tr("adbd 运行中") : tr("UDC: %1").arg(udc))
                        : tr("未运行"));
    } else if (jobId == m_actionJob) {
        m_actionJob = -1;
        refresh();
    }
}

HMI_REGISTER_SERVICE("adb", AdbService)
