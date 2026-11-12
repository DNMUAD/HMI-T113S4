#include "wifipage.h"
#include "commandinvoker.h"
#include "logpanel.h"
#include "pagefactory.h"
#include "statusbus.h"
#include "wifiscanmodel.h"
#include "wifiservice.h"

#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>

WifiPage::WifiPage(QWidget *parent)
    : BasePage(tr("WiFi"), parent)
    , m_ssid(nullptr)
    , m_password(nullptr)
    , m_scanView(nullptr)
    , m_rawView(nullptr)
    , m_model(new WifiScanModel(this))
    , m_waitingSequence(false)
{
}

void WifiPage::buildContent()
{
    setHint(tr("开机已由 /etc/init.d/S41wifi 装好驱动、拉起 wlan0 与 wifi_daemon，"
               "本页只做 STA 模式与联网：检查链路 → 扫描 → 连接。"
               " 扫描走 wpa_cli（wifi -s 的结果只打到板端串口，程序读不到）。"
               " 跳线帽：左 RS485 / 右 以太网"));

    m_ssid = new QLineEdit(this);
    m_ssid->setPlaceholderText(tr("SSID（可从下方扫描结果双击带入）"));
    addWidget(m_ssid);

    m_password = new QLineEdit(this);
    m_password->setPlaceholderText(tr("密码"));
    m_password->setEchoMode(QLineEdit::Password);
    addWidget(m_password);

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("检查链路"), []() { WifiService::instance()->prepare(); },
                              tr("确认 wlan0 + wifi_daemon，缺则跑 S41wifi start"), true), 0, 0);
    grid->addWidget(newButton(tr("扫描"), []() { WifiService::instance()->scan(); },
                              tr("wpa_cli（首次会先切 STA），失败回退 iw")), 0, 1);
    grid->addWidget(newButton(tr("连接"), [this]() {
        WifiService::instance()->connectTo(m_ssid->text(), m_password->text());
    }, tr("wifi -c ssid pass"), true), 1, 0);
    grid->addWidget(newButton(tr("一键联网"), [this]() {
        // 命令模式：服务给出一条 CommandSequence，交给 Invoker 排队串行执行
        m_waitingSequence = true;
        CommandInvoker::instance()->enqueue(
            WifiService::instance()->makeConnectSequence(m_ssid->text(), m_password->text()));
    }, tr("确保链路 → STA → 连接 → 取 IP（串行）")), 1, 1);
    grid->addWidget(newButton(tr("断开"), []() { WifiService::instance()->disconnect(); },
                              tr("wifi -d")), 2, 0);
    grid->addWidget(newButton(tr("刷新 IP"), []() { WifiService::instance()->refresh(); },
                              tr("ip -4 addr show wlan0")), 2, 1);
    grid->addWidget(newButton(tr("重启链路"), []() { WifiService::instance()->restartLink(); },
                              tr("S41wifi stop + start（daemon 卡死时用）"), false), 3, 0);
    grid->addWidget(newButton(tr("AP 模式"), []() { WifiService::instance()->initAp(); },
                              tr("进阶：wifi -o ap")), 3, 1);

    // Model/View：列表只认模型，解析规则全在 WifiScanModel 里
    m_scanView = new QListView(this);
    m_scanView->setObjectName(QStringLiteral("wifiScanView"));
    m_scanView->setModel(m_model);
    m_scanView->setMinimumHeight(150);
    m_scanView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_scanView->setAlternatingRowColors(true);
    addWidget(m_scanView);

    m_rawView = new QPlainTextEdit(this);
    m_rawView->setReadOnly(true);
    m_rawView->setMaximumBlockCount(120);
    m_rawView->setMinimumHeight(80);
    m_rawView->setPlaceholderText(tr("命令原始输出（排查用，解析规则见 WifiScanModel）"));
    addWidget(m_rawView);

    connect(WifiService::instance(), &WifiService::scanResult, this, &WifiPage::onScanResult);
    connect(WifiService::instance(), &WifiService::prepared, this, [this](bool ok) {
        setStatus(ok ? tr("WiFi 链路就绪，可以扫描并连接") : tr("链路准备失败，详情见日志"));
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
        setStatus(tr("扫描完成，但未解析到 SSID（原始输出见下方文本框）"));
    else
        setStatus(tr("扫描到 %1 个热点，双击可带入 SSID").arg(m_model->count()));
}

void WifiPage::onAccessPointActivated(const QModelIndex &index)
{
    const WifiAccessPoint ap = m_model->accessPoint(index.row());
    if (ap.ssid.isEmpty())
        return;
    m_ssid->setText(ap.ssid);
    m_password->setFocus();
    setStatus(tr("已选择 %1（%2）").arg(ap.ssid, m_model->data(index, WifiScanModel::DetailRole).toString()));
}

void WifiPage::onLinkChanged()
{
    const DeviceStatus status = StatusBus::instance()->status(QStringLiteral("wifi"));
    switch (status.state) {
    case DeviceReady:
        setStatus(tr("已连接 · IP %1").arg(status.detail));
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
