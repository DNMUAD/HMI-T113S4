#ifndef UPGRADESERVICE_H
#define UPGRADESERVICE_H

#include "deviceservice.h"

#include <QList>
#include <QString>

class QTimer;

// U 盘升级引擎。
//
// 架构参考 qt/qtotaupdate（Qt 官方 Embedded Linux OTA）的「清单 → 校验 → 落地 →
// 原子切换 → 回滚」流程，但我们的板端是 buildroot 直接铺文件、没有 ostree，
// 所以把「网络下载 + ostree deploy」换成「U 盘读包 + rename 覆盖」：
//
//   1. 扫描可移动卷，找 <挂载点>/hmi-upgrade/manifest.json
//   2. 逐文件算 SHA-256，与清单比对（不一致一律不装）
//   3. 先复制到同文件系统的 staging 目录，再 rename 到目标位置
//      —— rename 是原子的，且 Linux 允许替换正在运行的可执行文件（cp 会 ETXTBSY）
//   4. 覆盖前把原文件备份到 .bak，任一步失败按备份回滚
//   5. 写 upgrade-state.json 标记「待重启生效」，由页面提示重启
//
// 安全边界：清单里的相对路径禁止 .. 与绝对路径，target 只认 bin/i18n/config 三类，
// 目标位置写死在 targetPathFor() 里，包无法把文件写到别处。
class UpgradeService : public DeviceService
{
    Q_OBJECT
public:
    enum State {
        Idle,           // 还没扫过
        NoMedia,        // 没有可移动卷
        NoPackage,      // 有卷但没有升级包
        PackageFound,   // 找到包，未校验
        Verifying,
        Verified,       // 校验通过，可安装
        VerifyFailed,
        Installing,
        Installed,      // 已覆盖，等待重启
        InstallFailed,
        RollingBack,
        RolledBack
    };
    Q_ENUM(State)

    struct FileEntry
    {
        QString target;      // bin / i18n / config
        QString path;        // 包内相对路径
        QString sha256;      // 期望值，可为空表示不校验
        qint64  size;
        bool    executable;
    };

    struct Package
    {
        Package() : size(0) {}
        QString rootPath;        // U 盘上的 hmi-upgrade 目录
        QString product;
        QString version;
        QString minVersion;
        QString notes;
        QList<FileEntry> files;
        qint64  size;            // 全部文件字节数
    };

    static UpgradeService *instance();

    void refresh() override;          // 周期探测：只判定有没有 U 盘
    void scan();                      // 完整扫描：找挂载点 + 解析清单
    void verify();                    // 逐文件校验 SHA-256
    void install();                   // 覆盖安装（失败自动回滚）
    void rollback();                  // 用 .bak 恢复上一版
    void restart();                   // 板端 sync + reboot

    bool hasPackage() const;
    const Package &package() const;
    QString mediaPath() const;        // 当前 U 盘挂载点
    State state() const;
    int progress() const;
    QString message() const;
    QString currentVersion() const;   // 程序自身版本
    QString pendingVersion() const;   // 已安装待重启生效的版本
    bool needsRestart() const;

    // 包根目录名与候选挂载根，可被 ini 的 [upgrade] 段覆盖
    static QString packageDirName();
    static QStringList mediaRoots();

signals:
    void stateChanged(UpgradeService::State state);
    void progressChanged(int percent, const QString &text);
    void packageChanged();

private:
    explicit UpgradeService(QObject *parent = nullptr);

    void setState(State state, const QString &message = QString());
    void setProgress(int percent, const QString &text);
    void stepVerify();
    void stepInstall();
    bool checkEntry(const FileEntry &entry, QString *error) const;
    static QString targetPathFor(const FileEntry &entry);
    static QString stateFilePath();
    static bool writeStateFile(const QString &version, const QString &backupDir);

    Package m_package;
    QString m_mediaRoot;
    State   m_state;
    int     m_progress;
    QString m_message;
    QString m_pendingVersion;
    int     m_step;                   // 当前处理到第几个文件
    QString m_staging;                // 本次安装的 staging 目录
    QList<QPair<QString, QString> > m_backups;   // 目标 → 备份
};

#endif // UPGRADESERVICE_H
