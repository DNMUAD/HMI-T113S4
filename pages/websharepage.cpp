#include "websharepage.h"
#include "logpanel.h"
#include "pagefactory.h"
#include "qrcodewidget.h"
#include "webshareservice.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

WebSharePage::WebSharePage(QWidget *parent)
    : BasePage(tr("网页管理"), parent)
    , m_qr(nullptr)
    , m_url(nullptr)
    , m_info(nullptr)
    , m_roots(nullptr)
    , m_toggle(nullptr)
    , m_timer(nullptr)
{
}

void WebSharePage::buildContent()
{
    setHint(tr("手机与设备连同一局域网（WiFi 或网线），扫右侧二维码即可浏览 / 上传 / 下载 / 删除文件。"
               " 可访问目录限定在白名单内；本程序以 root 运行，请勿把 / 根目录加进白名单。"));

    QWidget *row = new QWidget(this);
    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(18);

    m_qr = new QrCodeWidget(row);
    m_qr->setFixedSize(228, 228);
    rowLayout->addWidget(m_qr, 0, Qt::AlignTop);

    QWidget *column = new QWidget(row);
    QVBoxLayout *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(8);

    m_url = new QLabel(tr("地址：-"), column);
    m_url->setObjectName(QStringLiteral("pageStatus"));
    m_url->setWordWrap(true);
    m_url->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_url->setMinimumHeight(48);
    columnLayout->addWidget(m_url);

    m_info = new QLabel(tr("状态：未启动"), column);
    m_info->setObjectName(QStringLiteral("pageStatus"));
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(48);
    columnLayout->addWidget(m_info);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    m_toggle = newButton(tr("启动网页服务"), [this]() { toggleServer(); }, tr("默认端口 8080"), true);
    buttons->addWidget(m_toggle);
    buttons->addWidget(newButton(tr("复制地址"), [this]() { copyAddress(); },
                                 tr("粘贴到电脑浏览器")));
    buttons->addWidget(newButton(tr("本机打开"), [this]() { openLocal(); },
                                 tr("桌面调试用")));
    columnLayout->addLayout(buttons);
    columnLayout->addStretch(1);

    m_roots = new QLabel(column);
    m_roots->setObjectName(QStringLiteral("pageHint"));
    // 不折行：目录路径很长，折行会把右侧这一列顶高、与左边二维码高度不齐。
    // 超长时在 refresh 里截断并补省略号，全文放 tooltip。
    m_roots->setWordWrap(false);
    columnLayout->addWidget(m_roots);

    rowLayout->addWidget(column, 1);
    addWidget(row);

    connect(WebShareService::instance(), &WebShareService::changed,
            this, &WebSharePage::refreshUi);

    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &WebSharePage::refreshUi);

    refreshUi();
}

void WebSharePage::onEnter()
{
    refreshUi();
    if (m_timer)
        m_timer->start();
}

void WebSharePage::onLeave()
{
    if (m_timer)
        m_timer->stop();
}

void WebSharePage::refreshUi()
{
    WebShareService *service = WebShareService::instance();
    const bool running = service->isRunning();

    if (m_toggle) {
        m_toggle->setText(running ? tr("停止网页服务") : tr("启动网页服务"));
        m_toggle->setDescription(running ? tr("释放端口 %1").arg(service->port())
                                         : tr("默认端口 8080"));
        m_toggle->setAccent(!running);
    }

    if (running) {
        const QString link = service->url();
        if (link.isEmpty()) {
            m_url->setText(tr("地址：未获取到局域网 IP（请先连接 WiFi 或网线）"));
            m_qr->setText(QString());
        } else {
            m_url->setText(tr("地址：%1").arg(link));
            m_qr->setText(link);
        }

        QString detail = tr("状态：运行中 · 端口 %1 · 已连接 %2")
                             .arg(service->port()).arg(service->clients());
        if (!service->lastRequest().isEmpty())
            detail += tr("\n最近请求：%1").arg(service->lastRequest());
        m_info->setText(detail);
        setStatus(service->token().isEmpty()
                      ? tr("免令牌访问（仅限同一局域网内）")
                      : tr("已启用访问令牌，二维码已携带"));
    } else {
        m_url->setText(tr("地址：-"));
        m_qr->setText(QString());
        m_info->setText(tr("状态：未启动"));
        setStatus(tr("点「启动网页服务」后显示二维码"));
    }

    const QStringList roots = service->roots();
    if (roots.isEmpty()) {
        m_roots->setText(tr("可访问目录：无可用的白名单目录（检查 /etc/hmi.ini 的 web/roots）"));
        m_roots->setToolTip(QString());
    } else {
        // 目录多/路径长时单行放不下：只显示第一个 + 总数，整体再按字符数截断，
        // 避免这一行折行把右侧列撑高、与左侧二维码错位。全文放 tooltip。
        QString shown = roots.first();
        if (roots.size() > 1)
            shown += tr("  等 %1 个").arg(roots.size());
        if (shown.size() > 46)
            shown = shown.left(46) + QStringLiteral("…");
        m_roots->setText(tr("可访问目录：%1").arg(shown));
        m_roots->setToolTip(roots.join(QLatin1Char('\n')));
    }
}

void WebSharePage::toggleServer()
{
    WebShareService *service = WebShareService::instance();
    if (service->isRunning()) {
        service->stop();
    } else {
        if (!service->start())
            setStatus(tr("启动失败：端口被占用或网络未就绪，详情见日志"));
    }
    refreshUi();
}

void WebSharePage::copyAddress()
{
    const QString link = WebShareService::instance()->url();
    if (link.isEmpty()) {
        setStatus(tr("请先启动服务并确保设备已获取 IP"));
        return;
    }
    QGuiApplication::clipboard()->setText(link);
    LogPanel::post(tr("已复制地址：%1").arg(link), LogPanel::Ok);
    setStatus(tr("地址已复制到剪贴板"));
}

void WebSharePage::openLocal()
{
    const QString link = WebShareService::instance()->url();
    if (link.isEmpty()) {
        setStatus(tr("请先启动服务并确保设备已获取 IP"));
        return;
    }
    QDesktopServices::openUrl(QUrl(link));
}

HMI_REGISTER_PAGE("web", 100, QT_TR_NOOP("网页管理"), WebSharePage)
