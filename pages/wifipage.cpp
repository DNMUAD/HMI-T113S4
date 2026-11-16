#include "wifipage.h"
#include "commandinvoker.h"
#include "logpanel.h"
#include "pagefactory.h"
#include "statusbus.h"
#include "wifiscanmodel.h"
#include "wifiservice.h"
#include "uimetrics.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

WifiPage::WifiPage(QWidget *parent)
    : BasePage(tr("WiFi"), parent)
    , m_ssid(nullptr)
    , m_password(nullptr)
    , m_scanView(nullptr)
    , m_rawView(nullptr)
    , m_scanStack(nullptr)
    , m_advancedPanel(nullptr)
    , m_advancedBtn(nullptr)
    , m_model(new WifiScanModel(this))
    , m_waitingSequence(false)
{
}

void WifiPage::buildContent()
{
    // 提示只留「这页需要知道的一件事」。原来那段把链路分工、命令来源、
    // 跳线帽全写在一起，占掉四行高度，而页面上真正要用的只有下面这些控件。
    setHint(tr("链路由开机脚本 S41wifi 拉起，扫描走 wpa_cli。"));

    addWidget(buildCredentialRow());

    // 常用操作一行四个，全部不带副标题：卡片按钮不带说明时是 42px 单行，
    // 带说明就变 58px，四个按钮排一行必须用单行高度才按得准。
    QWidget *actions = new QWidget(this);
    QHBoxLayout *row = new QHBoxLayout(actions);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UiMetrics::PageSpacing);

    row->addWidget(newButton(tr("扫描"), []() { WifiService::instance()->scan(); },
                             QString(), false), 1);
    row->addWidget(newButton(tr("连接"), [this]() {
        WifiService::instance()->connectTo(m_ssid->text(), m_password->text());
    }, QString(), true), 1);
    row->addWidget(newButton(tr("断开"), []() { WifiService::instance()->disconnect(); },
                             QString(), false), 1);

    m_advancedBtn = newButton(tr("更多"), [this]() { toggleAdvanced(); },
                              QString(), false);
    row->addWidget(m_advancedBtn, 1);
    addWidget(actions);

    // 扫描列表与诊断面板互斥占用同一块区域。
    //
    // 为什么不上下堆叠：480 高的屏，扣掉状态栏/日志/标题/提示/状态行/输入行/
    // 常用按钮之后，内容区只剩一百多像素。堆叠的话展开诊断必然把页面顶出
    // 滚动条，一滚「更多」按钮自己就看不见了，用户想收起还得先滚回去。
    // 互斥显示总高度基本不变，永远不需要滚 —— 而且「挑热点」和「看诊断输出」
    // 本来也不会同时进行。
    // 144 而不是 150：这是实测出来的数（用临时程序量过）。
    // 页面上其它控件加起来恰好比视口多 6px —— 那 6px 不是内容，是 body 布局
    // 末尾那个 addStretch 之外的一点余量。留 150 就必然出现纵向滚动条，
    // 而滚动条一出现，「更多」按钮就会在滚动时被带走，用户会以为它消失了。
    m_scanStack = new QStackedWidget(this);
    m_scanStack->setFixedHeight(144);
    addWidget(m_scanStack);

    m_scanView = new QListView(m_scanStack);
    m_scanView->setObjectName(QStringLiteral("wifiScanView"));
    m_scanView->setModel(m_model);
    m_scanView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_scanView->setAlternatingRowColors(true);
    m_scanStack->addWidget(m_scanView);          // index 0

    m_advancedPanel = buildAdvancedPanel();
    m_scanStack->addWidget(m_advancedPanel);     // index 1
    m_scanStack->setCurrentIndex(0);             // 默认看扫描结果

    connect(WifiService::instance(), &WifiService::scanResult, this, &WifiPage::onScanResult);
    connect(WifiService::instance(), &WifiService::prepared, this, [this](bool ok) {
        setStatus(ok ? tr("链路就绪") : tr("链路准备失败，详见日志"));
    });
    connect(m_scanView, &QListView::activated, this, &WifiPage::onAccessPointActivated);
    connect(StatusBus::instance(), &StatusBus::changed, this, [this](const DeviceStatus &status) {
        if (status.device == QStringLiteral("wifi"))
            onLinkChanged();
    });
    connect(CommandInvoker::instance(), &CommandInvoker::sequenceFinished,
            this, &WifiPage::onSequenceFinished);

    setStatus(tr("未连接"));
}

// SSID 与密码并排一行：两个都是短文本，各占半行比上下各一行省出一整行高度。
QWidget *WifiPage::buildCredentialRow()
{
    QWidget *holder = new QWidget(this);
    QHBoxLayout *row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(UiMetrics::PageSpacing);

    m_ssid = new QLineEdit(holder);
    m_ssid->setPlaceholderText(tr("SSID"));
    row->addWidget(m_ssid, 1);

    m_password = new QLineEdit(holder);
    m_password->setPlaceholderText(tr("密码"));
    m_password->setEchoMode(QLineEdit::Password);
    row->addWidget(m_password, 1);

    return holder;
}

// 诊断操作与原始输出放在一个面板里，和扫描列表互斥显示同一块区域。
//
// 相对原来的 8 键平铺，这里砍掉了「刷新 IP」：进入本页、扫描结束、连接结束
// 都会自动 refresh()，手动刷这个按钮从来没有单独存在的必要。
QWidget *WifiPage::buildAdvancedPanel()
{
    QWidget *panel = new QWidget(this);
    QVBoxLayout *col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(UiMetrics::PageSpacing);

    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(UiMetrics::PageSpacing);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    // 诊断按钮一律不带副标题：带说明是 58px，四个挤进 150 高的区域就顶到边了
    grid->addWidget(newButton(tr("检查链路"), []() { WifiService::instance()->prepare(); },
                              QString(), false), 0, 0);
    grid->addWidget(newButton(tr("重启链路"), []() { WifiService::instance()->restartLink(); },
                              QString(), false), 0, 1);
    grid->addWidget(newButton(tr("一键联网"), [this]() {
        // 命令模式：服务给出一条 CommandSequence，交给 Invoker 排队串行执行
        m_waitingSequence = true;
        CommandInvoker::instance()->enqueue(
            WifiService::instance()->makeConnectSequence(m_ssid->text(), m_password->text()));
    }, QString(), false), 1, 0);
    grid->addWidget(newButton(tr("AP 模式"), []() { WifiService::instance()->initAp(); },
                              QString(), false), 1, 1);
    col->addLayout(grid);

    m_rawView = new QPlainTextEdit(panel);
    m_rawView->setReadOnly(true);
    m_rawView->setMaximumBlockCount(120);
    // 必须给固定上限：QPlainTextEdit 的 sizeHint 很大，放进 QStackedWidget 后
    // 会按它的建议高度把整个区域撑高，页面立刻冒出滚动条。
    m_rawView->setMinimumHeight(46);
    m_rawView->setMaximumHeight(46);
    m_rawView->setPlaceholderText(tr("命令原始输出"));
    col->addWidget(m_rawView);

    return panel;
}

// 展开 / 收起诊断面板：只切换 QStackedWidget 的当前页，不改变整体高度，
// 因此不需要任何滚动补偿（原因见 buildContent 里的注释）。
void WifiPage::toggleAdvanced()
{
    const bool advanced = (m_scanStack->currentIndex() == 1);
    m_scanStack->setCurrentIndex(advanced ? 0 : 1);
    m_advancedBtn->setText(advanced ? tr("更多") : tr("收起"));
}

void WifiPage::onEnter()
{
    WifiService::instance()->refresh();
    onLinkChanged();
}

void WifiPage::onScanResult(const QString &raw)
{
    m_model->setRawOutput(raw);
    // 原始输出框同样要解码：否则中文 SSID 在这里显示成 \xe8\x8f\xa0...，
    // 而上方列表已经是「菠萝手机」，两边对不上会让人以为是两个热点。
    m_rawView->setPlainText(WifiScanModel::decodeEscaped(raw));
    if (m_model->count() == 0)
        setStatus(tr("未扫到热点"));
    else
        setStatus(tr("扫到 %1 个热点，双击带入").arg(m_model->count()));
}

void WifiPage::onAccessPointActivated(const QModelIndex &index)
{
    const WifiAccessPoint ap = m_model->accessPoint(index.row());
    if (ap.ssid.isEmpty())
        return;
    m_ssid->setText(ap.ssid);
    m_password->setFocus();      // 接着输密码：焦点交过去，软键盘自动弹出
    setStatus(tr("已选 %1（%2）").arg(ap.ssid, m_model->data(index, WifiScanModel::DetailRole).toString()));
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

void WifiPage::onSequenceFinished(const QString &description, bool ok, const QString &summary)
{
    if (!m_waitingSequence)
        return;
    m_waitingSequence = false;
    if (!ok)
        setStatus(tr("「%1」失败：%2").arg(description, summary));
    WifiService::instance()->refresh();
}

// 自注册：MainWindow 遍历注册表建页与导航，本页顺序 10（总览为 0）
HMI_REGISTER_PAGE("wifi", 10, QT_TR_NOOP("WiFi"), WifiPage)
