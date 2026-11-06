#include "btpage.h"
#include "btservice.h"
#include "pagefactory.h"
#include "statusbus.h"

#include <QGridLayout>

BtPage::BtPage(QWidget *parent)
    : BasePage(tr("蓝牙"), parent)
{
}

void BtPage::buildContent()
{
    setHint(tr("M1 骨架：控制器状态查询可用；上电 / 扫描 / 配对接口已预留，待 M6 按 rootfs 蓝牙栈实现。"));

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("刷新状态"), []() { BtService::instance()->refresh(); }, tr("hciconfig -a"), true), 0, 0);
    grid->addWidget(newButton(tr("上电"), []() { BtService::instance()->powerOn(); }, tr("TODO(M6)")), 0, 1);
    grid->addWidget(newButton(tr("下电"), []() { BtService::instance()->powerOff(); }, tr("TODO(M6)")), 1, 0);
    grid->addWidget(newButton(tr("扫描设备"), []() { BtService::instance()->scan(); }, tr("TODO(M6)")), 1, 1);

    connect(StatusBus::instance(), &StatusBus::changed, this, [this](const DeviceStatus &status) {
        if (status.device != QStringLiteral("bt"))
            return;
        setStatus(tr("蓝牙: %1 · %2").arg(deviceStateName(status.state), status.detail));
    });
}

void BtPage::onEnter()
{
    BtService::instance()->refresh();
}

HMI_REGISTER_PAGE("bt", 20, QT_TR_NOOP("蓝牙"), BtPage)

