#include "upgradepage.h"

#include "pagefactory.h"
#include "upgradeservice.h"

#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QObject::tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QObject::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

QString targetName(const QString &target)
{
    if (target == QLatin1String("bin"))
        return QObject::tr("主程序");
    if (target == QLatin1String("i18n"))
        return QObject::tr("翻译");
    return QObject::tr("配置");
}

} // namespace

UpgradePage::UpgradePage(QWidget *parent)
    : BasePage(tr("U 盘升级"), parent)
    , m_info(nullptr)
    , m_progress(nullptr)
    , m_verifyBtn(nullptr)
    , m_installBtn(nullptr)
    , m_restartBtn(nullptr)
    , m_rollbackBtn(nullptr)
{
}

void UpgradePage::buildContent()
{
    setHint(tr("升级包放在 U 盘 hmi-upgrade/ 下（manifest.json + 主程序 + i18n/*.qm + hmi.ini），"
               "安装会覆盖这三类文件，完成后需重启生效。"));

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("sysInfo"));
    m_info->setWordWrap(true);
    addWidget(m_info);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("upgradeProgress"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(tr("%p%"));
    addWidget(m_progress);

    QGridLayout *grid = addGrid();

    m_verifyBtn = newButton(tr("校验升级包"), []() {
        UpgradeService::instance()->verify();
    }, tr("逐文件比对 SHA-256"));
    grid->addWidget(m_verifyBtn, 0, 0);

    m_installBtn = newButton(tr("立即升级"), [this]() {
        UpgradeService *svc = UpgradeService::instance();
        svc->install();
        // 安装是异步逐文件的，完成后由 onStateChanged 打开重启按钮
    }, tr("覆盖主程序/翻译/配置"), true);
    grid->addWidget(m_installBtn, 0, 1);

    m_restartBtn = newButton(tr("重启生效"), []() {
        UpgradeService::instance()->restart();
    }, tr("sync 后重启设备"));
    grid->addWidget(m_restartBtn, 1, 0);

    m_rollbackBtn = newButton(tr("回滚"), []() {
        UpgradeService::instance()->rollback();
    }, tr("用 .bak 恢复上一版"));
    grid->addWidget(m_rollbackBtn, 1, 1);

    UpgradeService *svc = UpgradeService::instance();
    connect(svc, &UpgradeService::stateChanged, this, &UpgradePage::onStateChanged);
    connect(svc, &UpgradeService::progressChanged, this, &UpgradePage::onProgress);
    connect(svc, &UpgradeService::packageChanged, this, &UpgradePage::refreshView);

    refreshView();
}

void UpgradePage::onEnter()
{
    // 进页面就扫一次：插着 U 盘时不必再点「重新扫描」
    UpgradeService::instance()->refresh();
    refreshView();
}

void UpgradePage::onStateChanged(UpgradeService::State state)
{
    UpgradeService *svc = UpgradeService::instance();
    refreshView();

    // 状态行给「下一步做什么」，不与信息框里的包信息重复
    switch (state) {
    case UpgradeService::PackageFound:
        setStatus(tr("点「校验升级包」检查完整性"));
        break;
    case UpgradeService::Verified:
        setStatus(tr("校验通过，可以点「立即升级」"));
        break;
    default:
        setStatus(svc->message());
        break;
    }
}

void UpgradePage::onProgress(int percent, const QString &text)
{
    m_progress->setValue(percent);
    if (!text.isEmpty() && UpgradeService::instance()->state() != UpgradeService::Installed)
        setStatus(text);
}

void UpgradePage::refreshView()
{
    UpgradeService *svc = UpgradeService::instance();
    const UpgradeService::State state = svc->state();

    // ---- 介质 / 版本 / 包内容，合成一个框，避免信息块太多把按钮挤出可视区 ----
    QStringList lines;
    lines << tr("介质：%1").arg(svc->mediaPath().isEmpty() ? tr("未检测到 U 盘")
                                                          : svc->mediaPath());
    lines << tr("当前版本：%1").arg(svc->currentVersion());
    if (svc->needsRestart())
        lines << tr("待生效：v%1（重启后生效）").arg(svc->pendingVersion());

    if (svc->hasPackage()) {
        const UpgradeService::Package pkg = svc->package();
        lines << tr("升级包：v%1（%2，%3 个文件）")
                     .arg(pkg.version, humanSize(pkg.size)).arg(pkg.files.size());
        if (!pkg.notes.isEmpty())
            lines << tr("说明：%1").arg(pkg.notes);
        QStringList items;
        for (const UpgradeService::FileEntry &f : pkg.files)
            items << QStringLiteral("%1 %2").arg(targetName(f.target), humanSize(f.size));
        lines << tr("内容：%1").arg(items.join(QStringLiteral(" · ")));
    } else {
        lines << tr("升级包：%1").arg(svc->message());
    }
    m_info->setText(lines.join(QLatin1Char('\n')));

    // ---- 按钮可用性 ----
    const bool busy = (state == UpgradeService::Verifying
                       || state == UpgradeService::Installing
                       || state == UpgradeService::RollingBack);
    m_verifyBtn->setEnabled(svc->hasPackage() && !busy
                            && state != UpgradeService::Verified);
    m_installBtn->setEnabled(state == UpgradeService::Verified);
    m_restartBtn->setEnabled(svc->needsRestart()
                             && (state == UpgradeService::Installed
                                 || state == UpgradeService::Idle));
    m_rollbackBtn->setEnabled(state == UpgradeService::InstallFailed
                              || state == UpgradeService::Installed);

    if (state == UpgradeService::Installed)
        setStatus(tr("已安装 v%1，点「重启生效」或手动重启").arg(svc->package().version));
}

HMI_REGISTER_PAGE("upgrade", 110, QT_TR_NOOP("U盘升级"), UpgradePage)
