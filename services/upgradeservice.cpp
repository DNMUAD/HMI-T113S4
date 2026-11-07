#include "upgradeservice.h"

#include "appsettings.h"
#include "logpanel.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStorageInfo>
#include <QTimer>

#include <cstdio>

namespace {

const char *kProduct = "HMI-T113S4";

// 包内相对路径必须安全：不能是绝对路径、不能含 .. 或盘符，
// 否则一个恶意/错误清单就能把文件写到任意位置。
bool safeRelativePath(const QString &p)
{
    if (p.trimmed().isEmpty())
        return false;
    if (QDir::isAbsolutePath(p) || p.startsWith(QLatin1Char('/')))
        return false;
    if (p.contains(QLatin1String("..")) || p.contains(QLatin1Char(':')))
        return false;
    return true;
}

// 只认这三类目标，具体落盘位置由 targetPathFor() 写死，包无法指定任意路径
bool knownTarget(const QString &t)
{
    return t == QLatin1String("bin") || t == QLatin1String("i18n")
        || t == QLatin1String("config");
}

// 语义化版本比较：1.2.10 > 1.2.9，非数字段按字符串比
int compareVersion(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const QString sa = i < pa.size() ? pa.at(i) : QStringLiteral("0");
        const QString sb = i < pb.size() ? pb.at(i) : QStringLiteral("0");
        bool okA = false, okB = false;
        const int ia = sa.toInt(&okA);
        const int ib = sb.toInt(&okB);
        if (okA && okB) {
            if (ia != ib)
                return ia < ib ? -1 : 1;
        } else {
            const int c = QString::compare(sa, sb);
            if (c != 0)
                return c < 0 ? -1 : 1;
        }
    }
    return 0;
}

} // namespace

UpgradeService *UpgradeService::instance()
{
    return hmi::singleton<UpgradeService>([]() { return new UpgradeService; });
}

UpgradeService::UpgradeService(QObject *parent)
    // 5s 轮询：只做「插没插 U 盘」的轻量判断，介质变化时才真正读清单
    : DeviceService(QStringLiteral("upgrade"), QObject::tr("U 盘升级"), 5000, parent)
    , m_state(Idle)
    , m_progress(0)
    , m_step(0)
{
    // 上次安装过、还没重启：把待生效版本读出来，页面顶部会提示
    QFile f(stateFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        m_pendingVersion = obj.value(QStringLiteral("version")).toString();
        f.close();
    }
}

QString UpgradeService::packageDirName()
{
    const QString v = AppSettings::instance()->value(QStringLiteral("upgrade/packageDir"));
    return v.trimmed().isEmpty() ? QStringLiteral("hmi-upgrade") : v.trimmed();
}

QStringList UpgradeService::mediaRoots()
{
    // ini 显式配置优先（桌面调试可以指一个普通目录当 U 盘用）
    const QString configured = AppSettings::instance()->value(QStringLiteral("upgrade/roots"));
    if (!configured.trimmed().isEmpty()) {
        QStringList out;
        const QStringList parts = configured.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            const QString t = p.trimmed();
            if (!t.isEmpty() && QFileInfo(t).isDir())
                out << QDir::cleanPath(QFileInfo(t).canonicalFilePath().isEmpty()
                                           ? t : QFileInfo(t).canonicalFilePath());
        }
        return out;
    }

    QStringList roots;
#ifdef Q_OS_WIN
    // 可移动卷；C: 永远排除（那是系统盘，别让升级包往那儿写）
    const QList<QStorageInfo> vols = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &v : vols) {
        if (!v.isValid() || !v.isReady() || v.isReadOnly())
            continue;
        const QString root = v.rootPath();
        if (root.startsWith(QStringLiteral("C:"), Qt::CaseInsensitive))
            continue;
        roots << QDir::cleanPath(root);
    }
#else
    // /proc/mounts 里挑 sd[b-z] / mmcblk 这类可移动设备的挂载点，
    // 常见于 /media、/run/media、/mnt 下（buildroot 视 udev 规则而定）
    QFile mounts(QStringLiteral("/proc/mounts"));
    if (mounts.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString all = QString::fromLocal8Bit(mounts.readAll());
        mounts.close();
        const QStringList lines = all.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList cols = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (cols.size() < 2)
                continue;
            const QString dev = cols.at(0);
            const QString mnt = cols.at(1);
            if (!dev.startsWith(QLatin1String("/dev/sd")) && !dev.contains(QLatin1String("mmcblk")))
                continue;
            if (mnt == QLatin1String("/") || mnt.startsWith(QLatin1String("/boot")))
                continue;
            if (!roots.contains(mnt))
                roots << mnt;
        }
    }
#endif
    return roots;
}

// ------------------------------------------------------------------ 扫描

void UpgradeService::scan()
{
    m_package = Package();
    m_mediaRoot.clear();
    m_step = 0;

    const QStringList roots = mediaRoots();
    if (roots.isEmpty()) {
        setState(NoMedia, tr("未检测到可移动存储"));
        emit packageChanged();
        return;
    }

    for (const QString &root : roots) {
        const QString dir = QDir::cleanPath(root + QLatin1Char('/') + packageDirName());
        const QString manifestPath = dir + QLatin1String("/manifest.json");
        if (!QFileInfo::exists(manifestPath))
            continue;

        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly)) {
            setState(NoPackage, tr("无法读取 %1").arg(manifestPath));
            emit packageChanged();
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();

        // ---- 清单校验：产品名、版本、文件表 ----
        const QString product = obj.value(QStringLiteral("product")).toString();
        if (!product.isEmpty() && product != QLatin1String(kProduct)) {
            setState(NoPackage, tr("升级包产品不匹配：%1").arg(product));
            emit packageChanged();
            return;
        }
        const QString version = obj.value(QStringLiteral("version")).toString().trimmed();
        if (version.isEmpty()) {
            setState(NoPackage, tr("清单缺少 version"));
            emit packageChanged();
            return;
        }

        Package pkg;
        pkg.rootPath   = dir;
        pkg.product    = product;
        pkg.version    = version;
        pkg.minVersion = obj.value(QStringLiteral("minVersion")).toString();
        pkg.notes      = obj.value(QStringLiteral("notes")).toString();

        const QJsonArray files = obj.value(QStringLiteral("files")).toArray();
        for (const QJsonValue &v : files) {
            const QJsonObject fo = v.toObject();
            FileEntry e;
            e.target     = fo.value(QStringLiteral("target")).toString();
            e.path       = fo.value(QStringLiteral("path")).toString();
            e.sha256     = fo.value(QStringLiteral("sha256")).toString().trimmed();
            e.size       = static_cast<qint64>(fo.value(QStringLiteral("size")).toDouble());
            e.executable = fo.value(QStringLiteral("mode")).toString().contains(QLatin1String("755"));

            if (!knownTarget(e.target) || !safeRelativePath(e.path)) {
                setState(NoPackage, tr("清单路径非法：%1 → %2").arg(e.target, e.path));
                emit packageChanged();
                return;
            }
            pkg.size += e.size;
            pkg.files << e;
        }

        if (pkg.files.isEmpty()) {
            setState(NoPackage, tr("清单里没有任何文件"));
            emit packageChanged();
            return;
        }

        m_package = pkg;
        m_mediaRoot = root;
        setState(PackageFound, tr("发现升级包 v%1（%2 个文件）").arg(version).arg(pkg.files.size()));
        setProgress(0, QString());
        emit packageChanged();
        return;
    }

    setState(NoPackage, tr("已插入存储，但没有 %1/manifest.json").arg(packageDirName()));
    emit packageChanged();
}

// ------------------------------------------------------------------ 校验

void UpgradeService::verify()
{
    if (m_package.files.isEmpty())
        return;
    if (m_state != PackageFound && m_state != VerifyFailed && m_state != NoPackage)
        return;

    m_step = 0;
    setState(Verifying, tr("正在校验…"));
    setProgress(0, tr("开始校验"));

    // 每 tick 处理一个文件，避免大文件把 UI 卡死
    QTimer::singleShot(0, this, &UpgradeService::stepVerify);
}

void UpgradeService::stepVerify()
{
    if (m_state != Verifying)
        return;

    if (m_step >= m_package.files.size()) {
        setProgress(100, tr("校验通过"));
        setState(Verified, tr("校验通过，可以升级"));
        return;
    }

    const FileEntry &e = m_package.files.at(m_step);
    QString error;
    if (!checkEntry(e, &error)) {
        setProgress(0, error);
        setState(VerifyFailed, error);
        LogPanel::post(tr("升级包校验失败：%1").arg(error), LogPanel::Error);
        return;
    }

    ++m_step;
    setProgress(m_step * 100 / m_package.files.size(),
                tr("已校验 %1/%2").arg(m_step).arg(m_package.files.size()));
    QTimer::singleShot(0, this, &UpgradeService::stepVerify);
}

bool UpgradeService::checkEntry(const FileEntry &e, QString *error) const
{
    const QString src = QDir::cleanPath(m_package.rootPath + QLatin1Char('/') + e.path);
    QFileInfo fi(src);
    if (!fi.exists() || !fi.isFile()) {
        *error = tr("包内缺少 %1").arg(e.path);
        return false;
    }
    if (e.size > 0 && fi.size() != e.size) {
        *error = tr("%1 大小不符（期望 %2，实际 %3）")
                     .arg(e.path).arg(e.size).arg(fi.size());
        return false;
    }
    if (e.sha256.isEmpty())
        return true;      // 清单没给校验和就只验存在性

    QFile f(src);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = tr("%1 无法读取：%2").arg(e.path, f.errorString());
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        *error = tr("%1 读取中断").arg(e.path);
        return false;
    }
    const QString actual = QString::fromLatin1(hash.result().toHex());
    if (actual.compare(e.sha256, Qt::CaseInsensitive) != 0) {
        *error = tr("%1 校验和不符").arg(e.path);
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ 安装

QString UpgradeService::targetPathFor(const FileEntry &entry)
{
#ifdef Q_OS_WIN
    // 桌面调试不能覆盖正在运行的 exe，落到工作目录下的沙箱里，
    // 这样复制/备份/回滚的完整逻辑仍然能跑通并被验证。
    const QString base = QCoreApplication::applicationDirPath()
                         + QStringLiteral("/_upgrade_target");
    if (entry.target == QLatin1String("bin"))
        return base + QStringLiteral("/QtApplication.exe");
    if (entry.target == QLatin1String("i18n"))
        return base + QStringLiteral("/i18n/") + QFileInfo(entry.path).fileName();
    return base + QStringLiteral("/hmi.ini");
#else
    // 板端实际部署位置（与 buildroot 的安装行一致）
    if (entry.target == QLatin1String("bin"))
        return QStringLiteral("/usr/bin/QtApplication");
    if (entry.target == QLatin1String("i18n"))
        return QStringLiteral("/usr/share/hmi/i18n/") + QFileInfo(entry.path).fileName();
    return QStringLiteral("/etc/hmi.ini");
#endif
}

void UpgradeService::install()
{
    if (m_state != Verified)
        return;

    m_step = 0;
    m_backups.clear();
    setState(Installing, tr("正在安装…"));
    setProgress(0, tr("开始安装"));
    QTimer::singleShot(0, this, &UpgradeService::stepInstall);
}

void UpgradeService::stepInstall()
{
    if (m_state != Installing)
        return;

    if (m_step >= m_package.files.size()) {
        // 全部覆盖完成，记录待生效版本
        writeStateFile(m_package.version, QString());
        m_pendingVersion = m_package.version;
        setProgress(100, tr("安装完成"));
        setState(Installed, tr("已安装 v%1，重启后生效").arg(m_package.version));
        LogPanel::post(tr("升级包 v%1 安装完成，%2 个文件已覆盖，等待重启")
                           .arg(m_package.version).arg(m_package.files.size()),
                       LogPanel::Ok);
        return;
    }

    const FileEntry &e = m_package.files.at(m_step);
    const QString dst = targetPathFor(e);
    const QString src = QDir::cleanPath(m_package.rootPath + QLatin1Char('/') + e.path);
    QString error;

    // 落盘：先写到同目录的 .new，再 rename —— rename 是原子的，
    // 且 Linux 允许替换正在运行的可执行文件（直接 cp 覆盖会 ETXTBSY）。
    QDir().mkpath(QFileInfo(dst).absolutePath());
    const QString stage = dst + QStringLiteral(".new");
    QFile::remove(stage);
    if (!QFile::copy(src, stage)) {
        error = tr("%1 复制失败").arg(e.path);
    } else if (e.executable
               && !QFile::setPermissions(stage, QFile::ReadOwner | QFile::WriteOwner
                                                    | QFile::ExeOwner | QFile::ReadGroup
                                                    | QFile::ExeGroup | QFile::ReadOther
                                                    | QFile::ExeOther)) {
        error = tr("%1 设置可执行权限失败").arg(e.path);
    }

    if (error.isEmpty()) {
        // 覆盖前备份，失败可按备份回滚
        if (QFileInfo::exists(dst)) {
            const QString bak = dst + QStringLiteral(".bak");
            QFile::remove(bak);
            if (!QFile::rename(dst, bak))
                error = tr("%1 备份失败").arg(dst);
            else
                m_backups.append(qMakePair(dst, bak));
        }
    }

    if (error.isEmpty()) {
        bool moved = false;
#ifdef Q_OS_WIN
        QFile::remove(dst);                 // Windows 的 rename 不能覆盖已存在文件
        moved = QFile::rename(stage, dst);
#else
        moved = (std::rename(QFile::encodeName(stage).constData(),
                             QFile::encodeName(dst).constData()) == 0);
#endif
        if (!moved) {
            error = tr("%1 替换失败").arg(dst);
            QFile::remove(stage);
        }
    }

    if (!error.isEmpty()) {
        LogPanel::post(tr("安装失败：%1，开始回滚").arg(error), LogPanel::Error);
        rollback();                          // 用已备份的文件恢复现场
        setState(InstallFailed, error);
        setProgress(0, error);
        return;
    }

    ++m_step;
    setProgress(m_step * 90 / m_package.files.size(),
                tr("已覆盖 %1/%2").arg(m_step).arg(m_package.files.size()));
    QTimer::singleShot(0, this, &UpgradeService::stepInstall);
}

// ------------------------------------------------------------------ 回滚

void UpgradeService::rollback()
{
    if (m_backups.isEmpty()) {
        setState(RolledBack, tr("没有可回滚的备份"));
        return;
    }

    setState(RollingBack, tr("正在回滚…"));
    int restored = 0;
    // 逆序恢复，最后写入的最先退回
    for (int i = m_backups.size() - 1; i >= 0; --i) {
        const QString dst = m_backups.at(i).first;
        const QString bak = m_backups.at(i).second;
        if (!QFileInfo::exists(bak))
            continue;
        QFile::remove(dst);
        if (QFile::rename(bak, dst))
            ++restored;
    }
    m_backups.clear();
    // 回滚意味着这次升级没生效，待重启标记必须一并清掉，
    // 否则页面会一直提示「待生效 vX」而实际已是旧版本。
    m_pendingVersion.clear();
    writeStateFile(QString(), QString());
    setState(RolledBack, tr("已回滚 %1 个文件").arg(restored));
    LogPanel::post(tr("升级回滚完成，恢复 %1 个文件").arg(restored), LogPanel::Warn);
}

// ------------------------------------------------------------------ 重启

void UpgradeService::restart()
{
#ifdef Q_OS_LINUX
    LogPanel::post(tr("正在同步并重启…"), LogPanel::Warn);
    // 先 sync 落盘再 reboot，避免刚写入的文件还在 page cache 里就断电
    QProcess::startDetached(QStringLiteral("sh"),
                            QStringList() << QStringLiteral("-c")
                                          << QStringLiteral("sync; sleep 1; reboot"));
#else
    LogPanel::post(tr("桌面环境不执行重启：请手动关闭程序并确认 %1")
                       .arg(QCoreApplication::applicationDirPath()
                            + QStringLiteral("/_upgrade_target")),
                   LogPanel::Warn);
#endif
}

// ------------------------------------------------------------------ 查询

bool UpgradeService::hasPackage() const
{
    return !m_package.rootPath.isEmpty() && !m_package.files.isEmpty();
}

const UpgradeService::Package &UpgradeService::package() const
{
    return m_package;
}

QString UpgradeService::mediaPath() const
{
    return m_mediaRoot;
}

UpgradeService::State UpgradeService::state() const
{
    return m_state;
}

int UpgradeService::progress() const
{
    return m_progress;
}

QString UpgradeService::message() const
{
    return m_message;
}

QString UpgradeService::currentVersion() const
{
    return QCoreApplication::applicationVersion();
}

QString UpgradeService::pendingVersion() const
{
    return m_pendingVersion;
}

bool UpgradeService::needsRestart() const
{
    return !m_pendingVersion.isEmpty()
        && m_pendingVersion != QCoreApplication::applicationVersion();
}

// ------------------------------------------------------------------ 周期探测

void UpgradeService::refresh()
{
    const QStringList roots = mediaRoots();

    if (roots.isEmpty()) {
        if (!m_mediaRoot.isEmpty()) {
            m_mediaRoot.clear();
            m_package = Package();
            setState(NoMedia, tr("U 盘已拔出"));
            emit packageChanged();
        } else if (m_state == Idle) {
            setState(NoMedia, tr("未检测到 U 盘"));
        }
        publish(DeviceOff, tr("无 U 盘"));
        return;
    }

    publish(DeviceReady, m_mediaRoot.isEmpty() ? tr("已插入") : m_mediaRoot);

    // 只在「刚插入」或「换了盘」时才读清单，避免每次轮询都解析 JSON
    if (m_mediaRoot.isEmpty() || !roots.contains(m_mediaRoot))
        scan();
}

// ------------------------------------------------------------------ 内部

void UpgradeService::setState(State state, const QString &message)
{
    m_state = state;
    if (!message.isEmpty())
        m_message = message;
    emit stateChanged(state);
}

void UpgradeService::setProgress(int percent, const QString &text)
{
    m_progress = qBound(0, percent, 100);
    emit progressChanged(m_progress, text);
}

QString UpgradeService::stateFilePath()
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QStringLiteral("/_upgrade_state.json");
#else
    return QStringLiteral("/etc/hmi-upgrade-state.json");
#endif
}

bool UpgradeService::writeStateFile(const QString &version, const QString &backupDir)
{
    // 传空版本 = 清除待生效标记（回滚时用）
    if (version.isEmpty()) {
        QFile::remove(stateFilePath());
        return true;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("version"), version);
    obj.insert(QStringLiteral("installedAt"),
               QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!backupDir.isEmpty())
        obj.insert(QStringLiteral("staging"), backupDir);

    QFile f(stateFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

HMI_REGISTER_SERVICE("upgrade", UpgradeService)
