#include "btpage.h"

#include "btscanmodel.h"
#include "btservice.h"
#include "pagefactory.h"
#include "statusbus.h"
#include "uimetrics.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// 配对确认的 UI 兜底：BlueZ 侧对端约 25s 超时，这里留 20s。
// 超时按「拒绝」处理，避免句柄悬着把整条 bluez mainloop 堵死。
const int kConfirmTimeoutMs = 20000;

// 扫描面板高度。实测取值：页面其它控件加完比视口多 6px 就会冒纵向滚动条，
// 而滚动条一出现按钮就被带走。144 是 WiFi 页量出来的可用高度，
// 这里用同一块区域的同一个值，保持两页观感一致。
const int kListHeight = 144;

bool isNumericOnly(quintptr handle, bool numericOnly)
{
    Q_UNUSED(handle)
    return numericOnly;
}

} // namespace

BtPage::BtPage(QWidget *parent)
    : BasePage(tr("蓝牙"), parent)
    , m_scanView(nullptr)
    , m_scanModel(new BtScanModel(this))
    , m_stack(nullptr)
    , m_emptyHint(nullptr)
    , m_confirmText(nullptr)
    , m_powerButton(nullptr)
    , m_scanButton(nullptr)
    , m_confirmButton(nullptr)
    , m_rejectButton(nullptr)
    , m_pairTimer(new QTimer(this))
    , m_pendingHandle(0)
    , m_adapterOn(false)
    , m_scanning(false)
{
}

void BtPage::buildContent()
{
    // 提示只留「这页需要知道的一件事」：上电是慢操作。
    // 真机 bt_manager_enable 实测 3.7 秒（板端 2026-11-18 日志：
    // 0s 进入 → 3.27s hci 就绪 → 3.66s BT is ON）。
    // 仍要提示：这几秒里按钮无反馈，不说明会以为点了没反应。
    setHint(tr("首次上电约需 4 秒；链路由 bt_init.sh 拉起 hci0 与 bluetoothd。"));

    addWidget(buildActionRow());

    m_stack = new QStackedWidget(this);
    m_stack->setFixedHeight(kListHeight);
    addWidget(m_stack);

    m_stack->addWidget(buildScanPanel());      // index 0
    m_stack->addWidget(buildConfirmPanel());   // index 1
    m_stack->setCurrentIndex(0);

    BtService *service = BtService::instance();
    connect(service, &BtService::deviceFound, this, &BtPage::onDeviceFound);
    connect(service, &BtService::deviceRemoved, this, &BtPage::onDeviceRemoved);
    connect(service, &BtService::rssiUpdated, this, &BtPage::onRssiUpdated);
    connect(service, &BtService::scanStateChanged, this, &BtPage::onScanStateChanged);
    connect(service, &BtService::bondStateChanged, this, &BtPage::onBondStateChanged);
    connect(service, &BtService::passkeyRequested, this, &BtPage::onPasskeyRequested);
    connect(service, &BtService::pairedListChanged, this, &BtPage::onPairedListChanged);
    connect(service, &BtService::adapterStateChanged, this, &BtPage::onAdapterStateChanged);

    // 状态行跟总线走，和别的页面一致
    connect(StatusBus::instance(), &StatusBus::changed, this, [this](const DeviceStatus &status) {
        if (status.device != QStringLiteral("bt"))
            return;
        setStatus(tr("蓝牙: %1 · %2").arg(deviceStateName(status.state), status.detail));
    });

    m_pairTimer->setSingleShot(true);
    m_pairTimer->setInterval(kConfirmTimeoutMs);
    connect(m_pairTimer, &QTimer::timeout, this, &BtPage::onPairTimeout);

    m_adapterOn = service->adapterOn();
    refreshButtons();
    setEmptyHint(tr("尚未扫描"));
    setStatus(tr("未上电"));
}

// 常用操作一行三个，全部不带副标题（带说明会变 58px，
// 四个排一行在 800 宽下按不准；三个单行 42px 刚好）。
QWidget *BtPage::buildActionRow()
{
    QWidget *actions = new QWidget(this);
    QHBoxLayout *row = new QHBoxLayout(actions);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UiMetrics::PageSpacing);

    m_powerButton = newButton(tr("上电"), [this]() {
        if (m_adapterOn)
            BtService::instance()->powerOff();
        else
            BtService::instance()->powerOn();
        // 状态等 adapterStateChanged 回来再刷新，这里先禁用防连点
        refreshButtons();
    }, QString(), true);
    row->addWidget(m_powerButton, 1);

    m_scanButton = newButton(tr("扫描"), [this]() {
        BtService *service = BtService::instance();
        if (m_scanning) {
            service->stopScan();
            return;
        }
        // 每次开始扫描都清空：库的 start_scan 首句会 bt_remove_unpaired_devices()，
        // 已发现未配对的会被从 BlueZ 缓存删掉，本地累加会留下幽灵条目。
        m_scanModel->clear();
        setEmptyHint(tr("扫描中…"));
        service->scan();
        refreshButtons();
    }, QString(), false);
    row->addWidget(m_scanButton, 1);

    row->addWidget(newButton(tr("已配对"), [this]() {
        m_scanView->setModel(m_scanModel);
        setEmptyHint(tr("读取已配对列表…"));
        BtService::instance()->reloadPaired();
    }, QString(), false), 1);

    return actions;
}

QWidget *BtPage::buildScanPanel()
{
    QWidget *panel = new QWidget(this);
    QVBoxLayout *col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    m_scanView = new QListView(panel);
    m_scanView->setObjectName(QStringLiteral("btScanView"));
    m_scanView->setModel(m_scanModel);
    m_scanView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_scanView->setAlternatingRowColors(true);
    connect(m_scanView, &QListView::activated, this, &BtPage::onDeviceActivated);
    col->addWidget(m_scanView, 1);

    // 空列表时盖一句提示。用独立 QLabel 而不是 placeholder：
    // QListView 没有 placeholder，加一层 label 最省事，
    // 而且它和列表在同一块区域，不额外占高度。
    m_emptyHint = new QLabel(panel);
    m_emptyHint->setObjectName(QStringLiteral("pageStatus"));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    col->addWidget(m_emptyHint, 0);

    return panel;
}

// 配对确认面板：和扫描列表互斥占同一块高度，所以整体高度不变、不触发滚动。
// 板端单窗口，不能用 QDialog 弹窗。
QWidget *BtPage::buildConfirmPanel()
{
    QWidget *panel = new QWidget(this);
    QVBoxLayout *col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(UiMetrics::PageSpacing);

    m_confirmText = new QLabel(panel);
    m_confirmText->setObjectName(QStringLiteral("pageStatus"));
    m_confirmText->setWordWrap(true);
    m_confirmText->setAlignment(Qt::AlignCenter);
    col->addWidget(m_confirmText, 1);

    QWidget *buttons = new QWidget(panel);
    QHBoxLayout *row = new QHBoxLayout(buttons);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UiMetrics::PageSpacing);

    m_confirmButton = newButton(tr("确认配对"), [this]() { resolvePendingPasskey(true); },
                                QString(), true);
    row->addWidget(m_confirmButton, 1);

    m_rejectButton = newButton(tr("拒绝"), [this]() { resolvePendingPasskey(false); },
                               QString(), false);
    row->addWidget(m_rejectButton, 1);

    col->addWidget(buttons);
    return panel;
}

void BtPage::onEnter()
{
    BtService *service = BtService::instance();
    m_adapterOn = service->adapterOn();
    m_scanning = service->scanning();
    refreshButtons();
    // 不在这里调 scan()：进页面时若还没上电，自动扫描只会白跑一次。
}

void BtPage::onLeave()
{
    // 离开页面时停扫描：蓝牙 discovery 会持续占射频，
    // 用户在别的页面时没必要一直扫。
    if (m_scanning)
        BtService::instance()->stopScan();
}

void BtPage::refreshButtons()
{
    if (m_powerButton)
        m_powerButton->setText(m_adapterOn ? tr("下电") : tr("上电"));
    if (m_scanButton) {
        m_scanButton->setText(m_scanning ? tr("停止") : tr("扫描"));
        m_scanButton->setEnabled(m_adapterOn);
    }
}

void BtPage::setEmptyHint(const QString &text)
{
    if (!m_emptyHint)
        return;
    // 有内容时把提示藏掉，避免占着列表底部一行高度
    m_emptyHint->setText(text);
    m_emptyHint->setVisible(text.isEmpty() ? false : m_scanModel->count() == 0);
}

void BtPage::onAdapterStateChanged(bool on)
{
    m_adapterOn = on;
    if (!on) {
        m_scanning = false;
        m_scanModel->clear();
        setEmptyHint(tr("未上电"));
    } else {
        setEmptyHint(tr("已就绪，点「扫描」发现设备"));
    }
    refreshButtons();
}

void BtPage::onScanStateChanged(int state)
{
    m_scanning = (state == BtScanStarted);
    if (!m_scanning && m_scanModel->count() == 0)
        setEmptyHint(tr("未发现设备"));
    refreshButtons();
}

void BtPage::onDeviceFound(const BtDeviceRecord &device)
{
    m_scanModel->addOrUpdate(device);
    if (m_scanModel->count() > 0)
        setEmptyHint(QString());
    setStatus(tr("发现 %1 台设备").arg(m_scanModel->count()));
}

void BtPage::onDeviceRemoved(const QString &address)
{
    m_scanModel->removeDevice(address);
    if (m_scanModel->count() == 0 && !m_scanning)
        setEmptyHint(tr("未发现设备"));
}

void BtPage::onRssiUpdated(const QString &address, int rssi)
{
    m_scanModel->updateRssi(address, rssi);
}

// 双击设备：已配对的直接连，未配对的先配对
void BtPage::onDeviceActivated(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    const BtDeviceRecord device = m_scanModel->deviceAt(index.row());
    if (!device.isValid())
        return;

    if (device.paired) {
        setStatus(tr("正在连接 %1…").arg(device.name.isEmpty() ? device.address : device.name));
        BtService::instance()->connectDevice(device.address);
        return;
    }

    setStatus(tr("正在配对 %1…").arg(device.name.isEmpty() ? device.address : device.name));
    BtService::instance()->pair(device.address);
}

void BtPage::onBondStateChanged(int state, const QString &address)
{
    switch (state) {
    case BtBondBonding:
        m_pendingAddress = address;
        setStatus(tr("配对中 · %1").arg(address));
        break;

    case BtBondBonded:
        m_scanModel->setPaired(address, true);
        setStatus(tr("配对成功 · %1").arg(address));
        showConfirm(false);
        break;

    case BtBondBondFailed:
        setStatus(tr("配对失败 · %1").arg(address));
        showConfirm(false);
        break;

    case BtBondUnbonded:
        m_scanModel->setPaired(address, false);
        setStatus(tr("已取消配对 · %1").arg(address));
        break;

    default:
        break;
    }
}

void BtPage::onPasskeyRequested(quintptr handle, const QString &address,
                                unsigned int passkey, bool numericOnly)
{
    m_pendingHandle = handle;
    m_pendingAddress = address;

    if (isNumericOnly(handle, numericOnly)) {
        // 数字确认：和手机上显示的 6 位码比对
        m_confirmText->setText(tr("与设备 %1 显示的配对码核对：\n%2")
                               .arg(address)
                               .arg(passkey, 6, 10, QLatin1Char('0')));
    } else {
        m_confirmText->setText(tr("设备 %1 请求授权，是否允许？").arg(address));
    }

    showConfirm(true);
    setStatus(tr("等待确认配对码"));
    m_pairTimer->start();
}

void BtPage::showConfirm(bool visible)
{
    if (!m_stack)
        return;
    m_stack->setCurrentIndex(visible ? 1 : 0);
}

void BtPage::resolvePendingPasskey(bool accept)
{
    if (m_pendingHandle == 0)
        return;

    m_pairTimer->stop();
    // handle 只能用一次，先交回再清
    BtService::instance()->confirmPairing(m_pendingHandle, accept);
    m_pendingHandle = 0;

    showConfirm(false);
    setStatus(accept ? tr("已确认配对") : tr("已拒绝配对"));
}

void BtPage::onPairTimeout()
{
    if (m_pendingHandle == 0)
        return;

    // 超时按拒绝处理：BlueZ 侧不回复会让整条 mainloop 停住，
    // 对端约 25s 超时，不能就这么挂着。
    resolvePendingPasskey(false);
    setStatus(tr("配对确认超时，已自动拒绝"));
}

void BtPage::onPairedListChanged(const QList<BtDeviceRecord> &devices)
{
    // 已配对列表直接并进扫描列表：蓝牙页只有一块列表区域，
    // 分两个列表必然上下堆叠，480 高放不下。
    for (int i = 0; i < devices.size(); ++i)
        m_scanModel->addOrUpdate(devices.at(i));

    if (m_scanModel->count() == 0)
        setEmptyHint(tr("没有已配对设备"));
    else
        setEmptyHint(QString());

    setStatus(tr("已配对 %1 台").arg(devices.size()));
}

HMI_REGISTER_PAGE("bt", 20, QT_TR_NOOP("蓝牙"), BtPage)
