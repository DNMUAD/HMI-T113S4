#include "wifipage.h"
#include "pagefactory.h"
#include "statusbus.h"
#include "uimetrics.h"
#include "wifiscanmodel.h"
#include "wifiservice.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStackedWidget>
#include <QStringListModel>
#include <QVBoxLayout>

namespace {

// 列表区固定高度：144 是实测值（见 wifipage.cpp 的历史注释），
// 保证 800x480 下整页不出现纵向滚动条。滚动条会带走「切换模式」按钮，
// 用户会以为按钮消失了。
const int kListHeight = 144;

// 密码规则：可打印 ASCII 字符（字母 / 数字 / 符号），至少 8 位。
//
// 字符集用 [!-~]，即 ASCII 0x21('!') 到 0x7E('~')：
//   · 覆盖大小写字母、数字、以及全部常见符号 !@#$%^&*()-_=+[]{};:'",.<>/?\|`~
//   · 不含空格(0x20)：WPA2 标准其实允许空格，但板端靠软键盘输入，
//     前导/尾随空格在密码框里看不出来（显示成 ●），极易输错且无法自查，
//     所以这里排除。真要支持就把范围改成 [ -~]。
//   · 不含中文和不可打印字符：WPA2-PSK 的 passphrase 只接受 ASCII。
//
// 写法注意：范围两端字符在 PCRE 里不需要转义，但别写成 [A-Za-z0-9!-~] ——
// 那样 !-~ 是独立范围，虽然结果一样，但可读性差且容易误改。
// 用 {8,} 不设上限：WPA2 最长 63 位，不要人为截断。
const char *kPasswordPattern = "^[!-~]{8,}$";

bool isValidPassword(const QString &text)
{
    static const QRegularExpression rx(QString::fromLatin1(kPasswordPattern));
    return rx.match(text).hasMatch();
}

} // namespace

WifiPage::WifiPage(QWidget *parent)
    : BasePage(tr("WiFi"), parent)
    , m_ssid(nullptr)
    , m_password(nullptr)
    , m_scanView(nullptr)
    , m_apView(nullptr)
    , m_listStack(nullptr)
    , m_model(new WifiScanModel(this))
    , m_modeBtn(nullptr)
    , m_restartBtn(nullptr)
    , m_scanBtn(nullptr)
    , m_connectBtn(nullptr)
    , m_disconnectBtn(nullptr)
    , m_apOpenBtn(nullptr)
    , m_apListBtn(nullptr)
    , m_apCloseBtn(nullptr)
{
}

void WifiPage::buildContent()
{
    setHint(tr("点击「切换模式」在 STA / AP 之间切换；「重启WiFi」会重跑 S41wifi。"));

    QWidget *inputRow = new QWidget(this);
    buildInputRow(inputRow);
    addWidget(inputRow);

    QWidget *buttonRow = new QWidget(this);
    buildButtonRow(buttonRow);
    addWidget(buttonRow);

    // 列表区：STA 显示扫描结果，AP 显示已连接设备。两者互斥占同一块高度，
    // 切换时总高度不变，因此不需要滚动。
    m_listStack = new QStackedWidget(this);
    m_listStack->setFixedHeight(kListHeight);

    m_scanView = new QListView(m_listStack);
    m_scanView->setObjectName(QStringLiteral("wifiScanView"));
    m_scanView->setModel(m_model);
    m_scanView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_scanView->setAlternatingRowColors(true);
    m_listStack->addWidget(m_scanView);         // index 0

    m_apView = new QListView(m_listStack);
    m_apView->setObjectName(QStringLiteral("wifiApView"));
    m_apView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_apView->setAlternatingRowColors(true);
    m_listStack->addWidget(m_apView);           // index 1

    addWidget(m_listStack);

    connect(WifiService::instance(), &WifiService::scanResult, this, &WifiPage::onScanResult);
    connect(WifiService::instance(), &WifiService::scanRefused, this, &WifiPage::onScanRefused);
    connect(WifiService::instance(), &WifiService::scanCountdown, this, &WifiPage::onScanCountdown);
    connect(WifiService::instance(), &WifiService::apClients, this, &WifiPage::onApClients);
    connect(WifiService::instance(), &WifiService::apClientsFailed,
            this, &WifiPage::onApClientsFailed);
    connect(WifiService::instance(), &WifiService::modeChanged, this, &WifiPage::onModeChanged);
    connect(WifiService::instance(), &WifiService::apOpened, this,
            [this](bool ok, const QString &detail) {
        if (ok)
            setStatus(tr("热点 %1 已开启").arg(detail));
        else if (!detail.isEmpty())
            setStatus(tr("热点已关闭"));
    });
    connect(m_scanView, &QListView::activated, this, &WifiPage::onItemActivated);
    connect(StatusBus::instance(), &StatusBus::changed, this, [this](const DeviceStatus &status) {
        if (status.device == QStringLiteral("wifi"))
            onLinkChanged();
    });

    applyMode();
    setStatus(tr("未连接"));
}

void WifiPage::buildInputRow(QWidget *parent)
{
    QHBoxLayout *row = new QHBoxLayout(parent);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UiMetrics::PageSpacing);

    m_ssid = new QLineEdit(parent);
    m_ssid->setPlaceholderText(tr("SSID"));
    row->addWidget(m_ssid, 1);

    m_password = new QLineEdit(parent);
    m_password->setPlaceholderText(tr("密码"));
    m_password->setEchoMode(QLineEdit::Password);

    // 正则限制：只允许字母/数字，至少 8 位。
    //
    // 用 QRegularExpressionValidator 而不是在提交时校验一次：
    // 板端没有物理键盘，软键盘是唯一输入手段，让非法字符根本打不进去
    // 比敲完再弹错误提示友好得多。
    //
    // 注意它只拦「用户编辑」——setText() / insert() 会绕过验证器。
    // AP 模式切过来时正是用 setText() 灌默认密码，所以提交前必须
    // 再调一次 isValidPassword()（见 doConnect / openAp）。
    m_password->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QString::fromLatin1(kPasswordPattern)), m_password));

    row->addWidget(m_password, 1);
}

// 四种按钮全部建好，按模式 hide()/show()。
// 不动态创建/销毁：按钮在自己被点中的回调里被删除会崩，
// 而且反复 new 会让布局抖动。
void WifiPage::buildButtonRow(QWidget *parent)
{
    QHBoxLayout *row = new QHBoxLayout(parent);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UiMetrics::PageSpacing);

    m_modeBtn = newButton(tr("切换模式"), [this]() { toggleMode(); }, QString(), true);
    row->addWidget(m_modeBtn, 1);

    m_restartBtn = newButton(tr("重启WiFi"), []() { WifiService::instance()->restartLink(); },
                             QString(), false);
    row->addWidget(m_restartBtn, 1);

    // ---- STA ----
    m_scanBtn = newButton(tr("扫描"), [this]() { doScan(); }, QString(), false);
    row->addWidget(m_scanBtn, 1);

    m_connectBtn = newButton(tr("连接"), [this]() { doConnect(); }, QString(), false);
    row->addWidget(m_connectBtn, 1);

    m_disconnectBtn = newButton(tr("断开"), [this]() { doDisconnect(); }, QString(), false);
    row->addWidget(m_disconnectBtn, 1);

    // ---- AP ----
    m_apOpenBtn = newButton(tr("开启"), [this]() {
        const QString ssid = m_ssid->text().trimmed().isEmpty()
                             ? QStringLiteral("T113S4") : m_ssid->text().trimmed();
        const QString password = m_password->text();
        // 热点密码同样是 WPA2，规则与 STA 一致：可打印 ASCII、至少 8 位。
        // 默认值 12345678 是 8 位数字，本身合法。
        if (!isValidPassword(password)) {
            setStatus(tr("密码至少 8 位，可用字母、数字、符号"));
            m_password->setFocus();
            return;
        }
        WifiService::instance()->openAp(ssid, password, 6, QStringLiteral("wpa2"));
    }, QString(), false);
    row->addWidget(m_apOpenBtn, 1);

    m_apListBtn = newButton(tr("连接列表"), []() { WifiService::instance()->listApClients(); },
                            QString(), false);
    row->addWidget(m_apListBtn, 1);

    m_apCloseBtn = newButton(tr("关闭"), []() { WifiService::instance()->closeAp(); },
                             QString(), false);
    row->addWidget(m_apCloseBtn, 1);
}

void WifiPage::applyMode()
{
    const bool ap = (WifiService::instance()->mode() == WifiService::ModeAp);

    m_scanBtn->setVisible(!ap);
    m_connectBtn->setVisible(!ap);
    m_disconnectBtn->setVisible(!ap);

    m_apOpenBtn->setVisible(ap);
    m_apListBtn->setVisible(ap);
    m_apCloseBtn->setVisible(ap);

    m_modeBtn->setText(ap ? tr("切到STA") : tr("切到AP"));

    // AP 模式下输入框是「热点自己的名字/密码」，给出与 STA 不同的占位提示
    if (ap) {
        m_ssid->setText(QStringLiteral("T113S4"));
        m_password->setText(QStringLiteral("12345678"));
        m_password->setEchoMode(QLineEdit::Normal);   // 热点密码要能看见核对
        m_ssid->setPlaceholderText(tr("热点名称"));
        m_password->setPlaceholderText(tr("热点密码 ≥8 位"));
        setListMode(true);
    } else {
        m_ssid->setPlaceholderText(tr("SSID"));
        m_password->setPlaceholderText(tr("密码 ≥8 位"));
        m_password->setEchoMode(QLineEdit::Password);
        setListMode(false);
    }
}

void WifiPage::setListMode(bool apClients)
{
    m_listStack->setCurrentIndex(apClients ? 1 : 0);
}

// 切换模式：
//   切到 STA → 直接开启 STA（wifi -o sta）
//   切到 AP  → 只执行 wifi -f sta（关掉 STA 让位），热点由「开启」单独开
void WifiPage::toggleMode()
{
    WifiService *svc = WifiService::instance();
    svc->setMode(svc->mode() == WifiService::ModeAp ? WifiService::ModeSta
                                                    : WifiService::ModeAp);
}

void WifiPage::onModeChanged()
{
    applyMode();
    WifiService::instance()->enterMode();
    const bool ap = (WifiService::instance()->mode() == WifiService::ModeAp);
    setStatus(ap ? tr("已切到 AP 模式") : tr("已切到 STA 模式"));
}

void WifiPage::doScan()
{
    // 不要在调服务前先置灰：服务可能因为前置条件不满足直接拒绝，
    // 那时不会有 scanResult / scanCountdown 回来，按钮就永久灰了。
    // 由 onScanCountdown(>0) 在真正开始倒计时时再置灰。
    WifiService::instance()->scan();
}

// 扫描被服务拒绝：恢复按钮 + 说明原因。
// 没有这条，doScan() 置的灰就再也恢复不了 —— 就是「扫描按钮一直是灰的」那个 bug。
void WifiPage::onScanRefused(const QString &reason)
{
    m_scanBtn->setEnabled(true);
    setStatus(reason);
}

// 扫描期间每秒一次：
//   seconds > 0  轮询中，显示剩余秒数（列表此时随扫随长）
//   seconds == 0 5 秒跑完，恢复按钮并给出最终数量
//   seconds < 0  被中止（离开页面 / stopScan）
void WifiPage::onScanCountdown(int seconds)
{
    if (seconds > 0) {
        m_scanBtn->setEnabled(false);
        setStatus(tr("扫描中… 剩余 %1 秒 · 已发现 %2 个")
                      .arg(seconds).arg(m_model->count()));
        return;
    }

    m_scanBtn->setEnabled(true);
    if (seconds < 0) {
        setStatus(tr("扫描已停止"));
        return;
    }
    // 5 秒结束：列表里已经有累积的结果了，这里只更新状态行
    setStatus(m_model->count() == 0
                  ? tr("未扫到热点")
                  : tr("扫描完成，共 %1 个热点，双击带入").arg(m_model->count()));
}

void WifiPage::doConnect()
{
    const QString ssid = m_ssid->text().trimmed();
    const QString password = m_password->text();

    if (ssid.isEmpty()) {
        setStatus(tr("请先选择或填写 SSID"));
        return;
    }
    // 验证器只保证「能输入的字符合法」，不保证长度够：
    // QRegularExpressionValidator 是按 PartialMatch 判定的，
    // 敲到第 3 位时它认为「可能还没输完」于是放行。
    // 所以长度/字符集的最终判定必须在提交这一刻做。
    if (!isValidPassword(password)) {
        setStatus(tr("密码至少 8 位，可用字母、数字、符号"));
        m_password->setFocus();
        return;
    }
    WifiService::instance()->connectTo(ssid, password);
}

void WifiPage::doDisconnect()
{
    WifiService::instance()->disconnect();
}

// 轮询扫描期间每秒调用一次，只负责刷新列表。
//
// 不要在这里 setEnabled(true) 或改状态行：扫描进行中按钮该保持禁用，
// 状态行的剩余秒数由 onScanCountdown 负责。这里越权改会让按钮在
// 扫描中途又变成可点，用户能点出第二条并行的扫描。
void WifiPage::onScanResult(const QString &raw)
{
    m_model->setRawOutput(raw);
}

void WifiPage::onItemActivated(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    const WifiAccessPoint ap = m_model->accessPoint(index.row());
    if (ap.ssid.isEmpty())
        return;
    m_ssid->setText(ap.ssid);
    m_password->setFocus();     // 接着输密码，软键盘自动弹出
    setStatus(tr("已选 %1").arg(ap.ssid));
}

void WifiPage::onApClients(const QList<ApClient> &clients)
{
    QStringList lines;
    lines.reserve(clients.size());
    for (int i = 0; i < clients.size(); ++i) {
        const ApClient &c = clients.at(i);
        // IP 反查不到时显示 "--"：ARP 表还没这条记录（设备刚连上），
        // 不是错误，再点一次「连接列表」通常就有了。
        lines.append(QStringLiteral("%1    %2")
                         .arg(c.ip.isEmpty() ? QStringLiteral("--") : c.ip, c.mac));
    }
    showApClients(lines);
    setStatus(clients.isEmpty() ? tr("暂无设备连接")
                                : tr("已连接 %1 台设备").arg(clients.size()));
}

void WifiPage::onApClientsFailed(const QString &reason)
{
    // 失败不往列表里塞假数据：清空列表 + 状态行说明原因，
    // 否则「读取失败」会像一台设备那样列在设备列表里。
    showApClients(QStringList());
    setStatus(tr("读取连接列表失败 · %1").arg(reason));
}

void WifiPage::showApClients(const QStringList &macs)
{
    // AP 设备列表很短（通常 0-5 台），用 QStringListModel 足够。
    // 复用同一个 model 只换内容：每点一次就 new 一个会持续泄漏，
    // 而且旧 model 被 setModel 换下后没人 delete。
    if (QStringListModel *model = qobject_cast<QStringListModel *>(m_apView->model()))
        model->setStringList(macs);
    else
        m_apView->setModel(new QStringListModel(macs, m_apView));
    m_listStack->setCurrentIndex(1);
}

void WifiPage::onLinkChanged()
{
    const DeviceStatus status = StatusBus::instance()->status(QStringLiteral("wifi"));
    switch (status.state) {
    case DeviceReady:
        setStatus(tr("已连接 · %1").arg(status.detail));
        break;
    case DeviceBusy:
        setStatus(tr("处理中 · %1").arg(status.detail));
        break;
    case DeviceError:
        setStatus(tr("错误 · %1").arg(status.detail));
        break;
    case DeviceOff:
        setStatus(tr("未连接 · %1").arg(status.detail));
        break;
    default:
        setStatus(tr("状态未知"));
        break;
    }
}

void WifiPage::onEnter()
{
    // 回到本页时扫描按钮必须可用：
    // 上一次离开时 onLeave() 停掉了扫描，页面可能停留在「禁用」状态
    // （stopScan 发的 scanCountdown(-1) 会恢复，但那是页面不可见时发的，
    // 槽函数照样执行；这里再兜一次底，避免任何路径下按钮留在灰态）。
    m_scanBtn->setEnabled(true);

    // 首次进入按所选模式开启；之后进入只刷新状态，
    // 避免每次翻页都重新下发 wifi -o sta 打断已建立的连接。
    WifiService *svc = WifiService::instance();
    if (!svc->linkReady())
        svc->enterMode();
    else
        svc->refresh();
    onLinkChanged();
}

void WifiPage::onLeave()
{
    // 离开页面立即停止扫描
    WifiService::instance()->stopScan();
}

HMI_REGISTER_PAGE("wifi", 10, QT_TR_NOOP("WiFi"), WifiPage)
