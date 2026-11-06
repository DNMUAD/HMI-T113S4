#include "adbpage.h"
#include "adbservice.h"
#include "pagefactory.h"
#include "statusbus.h"

#include <QGridLayout>

AdbPage::AdbPage(QWidget *parent)
    : BasePage(tr("ADB 调试"), parent)
{
}

void AdbPage::buildContent()
{
    setHint(tr("开关执行 sh /etc/init.d/S50adb_start start|stop；PC 端用 adb devices 观察变化。"));

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("开启 ADB"), []() { AdbService::instance()->start(); }, tr("S50adb_start start"), true), 0, 0);
    grid->addWidget(newButton(tr("关闭 ADB"), []() { AdbService::instance()->stop(); }, tr("S50adb_start stop")), 0, 1);
    grid->addWidget(newButton(tr("刷新状态"), []() { AdbService::instance()->refresh(); }, tr("pidof adbd + UDC")), 1, 0);

    connect(StatusBus::instance(), &StatusBus::changed, this, [this](const DeviceStatus &status) {
        if (status.device != QStringLiteral("adb"))
            return;
        setStatus(status.state == DeviceReady ? tr("ADB 运行中 · %1").arg(status.detail)
                                              : tr("ADB 未运行 · %1").arg(status.detail));
    });
}

void AdbPage::onEnter()
{
    AdbService::instance()->refresh();
}

HMI_REGISTER_PAGE("adb", 90, QT_TR_NOOP("ADB"), AdbPage)
