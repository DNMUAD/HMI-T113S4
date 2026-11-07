#include "fileservice.h"
#include "singleton.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

namespace {

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

} // namespace

FileService *FileService::instance()
{
    return hmi::singleton<FileService>([]() { return new FileService; });
}

FileService::FileService(QObject *parent)
    : QObject(parent)
{
}

void FileService::setRoots(const QStringList &roots)
{
    m_roots.clear();
    for (int i = 0; i < roots.size(); ++i) {
        const QString raw = roots.at(i).trimmed();
        if (raw.isEmpty())
            continue;
        const QString canon = QFileInfo(raw).canonicalFilePath();
        if (canon.isEmpty())
            continue;                       // 目录不存在（未插 U 盘）就跳过
        if (!m_roots.contains(canon))
            m_roots.append(canon);
    }
}

QStringList FileService::roots() const
{
    return m_roots;
}

bool FileService::isRoot(const QString &path) const
{
    const QString canon = canonical(path);
    return !canon.isEmpty() && m_roots.contains(canon);
}

QString FileService::canonical(const QString &path) const
{
    // 只接受绝对路径：板端是 /开头，Windows 是 C:/ 开头，用 isAbsolutePath 统一判断
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        return QString();

    const QString clean = QDir::cleanPath(path);
    if (clean.contains(QStringLiteral("/../")) || clean == QStringLiteral(".."))
        return QString();

    QString resolved = QFileInfo(clean).canonicalFilePath();
    if (resolved.isEmpty()) {
        // 目标尚不存在（新建/上传）：用父目录的 canonical 路径拼回来再校验
        const QFileInfo info(clean);
        const QString parentCanon = QFileInfo(info.absolutePath()).canonicalFilePath();
        if (parentCanon.isEmpty())
            return QString();
        resolved = QDir::cleanPath(parentCanon + QLatin1Char('/') + info.fileName());
    }

    for (int i = 0; i < m_roots.size(); ++i) {
        const QString &root = m_roots.at(i);
        if (resolved == root || resolved.startsWith(root + QLatin1Char('/')))
            return resolved;
    }
    return QString();
}

bool FileService::validName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 200)
        return false;
    if (trimmed == QStringLiteral(".") || trimmed == QStringLiteral(".."))
        return false;
    if (trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\')))
        return false;
    return true;
}

bool FileService::list(const QString &path, QList<Entry> *entries, QString *error) const
{
    if (entries)
        entries->clear();

    const QString dirPath = canonical(path);
    if (dirPath.isEmpty()) {
        setError(error, QStringLiteral("路径不在允许的目录范围内"));
        return false;
    }

    QDir dir(dirPath);
    if (!dir.exists()) {
        setError(error, QStringLiteral("目录不存在"));
        return false;
    }

    const QFileInfoList infos = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                  | QDir::Hidden,
                                                  QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (int i = 0; i < infos.size(); ++i) {
        const QFileInfo &info = infos.at(i);
        Entry entry;
        entry.name = info.fileName();
        entry.path = info.absoluteFilePath();
        entry.isDir = info.isDir();
        entry.size = entry.isDir ? 0 : info.size();
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
        entry.mtime = info.lastModified().toSecsSinceEpoch();
#else
        entry.mtime = static_cast<qint64>(info.lastModified().toTime_t());
#endif
        if (entries)
            entries->append(entry);
    }
    return true;
}

bool FileService::mkdir(const QString &parent, const QString &name, QString *error)
{
    if (!validName(name)) {
        setError(error, QStringLiteral("目录名不合法"));
        return false;
    }
    const QString parentPath = canonical(parent);
    if (parentPath.isEmpty()) {
        setError(error, QStringLiteral("父目录不在允许范围内"));
        return false;
    }
    const QString target = canonical(parentPath + QLatin1Char('/') + name.trimmed());
    if (target.isEmpty()) {
        setError(error, QStringLiteral("目标路径不合法"));
        return false;
    }
    if (QFileInfo::exists(target)) {
        setError(error, QStringLiteral("同名文件或目录已存在"));
        return false;
    }
    if (!QDir().mkdir(target)) {
        setError(error, QStringLiteral("创建目录失败（权限或只读文件系统）"));
        return false;
    }
    return true;
}

bool FileService::renameItem(const QString &path, const QString &newName, QString *error)
{
    if (!validName(newName)) {
        setError(error, QStringLiteral("名称不合法"));
        return false;
    }
    const QString source = canonical(path);
    if (source.isEmpty()) {
        setError(error, QStringLiteral("路径不在允许范围内"));
        return false;
    }
    if (m_roots.contains(source)) {
        setError(error, QStringLiteral("不允许重命名根目录"));
        return false;
    }

    const QFileInfo info(source);
    const QString target = canonical(info.absolutePath() + QLatin1Char('/') + newName.trimmed());
    if (target.isEmpty()) {
        setError(error, QStringLiteral("目标路径不合法"));
        return false;
    }
    if (QFileInfo::exists(target)) {
        setError(error, QStringLiteral("同名文件或目录已存在"));
        return false;
    }

    const bool ok = info.isDir() ? QDir().rename(source, target) : QFile::rename(source, target);
    if (!ok)
        setError(error, QStringLiteral("重命名失败"));
    return ok;
}

bool FileService::removeItem(const QString &path, bool recursive, QString *error)
{
    const QString target = canonical(path);
    if (target.isEmpty()) {
        setError(error, QStringLiteral("路径不在允许范围内"));
        return false;
    }
    if (m_roots.contains(target)) {
        setError(error, QStringLiteral("不允许删除根目录"));
        return false;
    }

    const QFileInfo info(target);
    if (info.isDir()) {
        QDir dir(target);
        if (!recursive && dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size() > 0) {
            setError(error, QStringLiteral("目录非空，需确认递归删除"));
            return false;
        }
        if (!dir.removeRecursively()) {
            setError(error, QStringLiteral("删除目录失败（权限或只读文件系统）"));
            return false;
        }
        return true;
    }

    if (!QFile::remove(target)) {
        setError(error, QStringLiteral("删除文件失败（权限或只读文件系统）"));
        return false;
    }
    return true;
}

bool FileService::usage(const QString &path, qint64 *freeBytes, qint64 *totalBytes) const
{
    const QString target = canonical(path);
    if (target.isEmpty())
        return false;

    const QStorageInfo storage(target);
    if (!storage.isValid() || !storage.isReady())
        return false;

    if (freeBytes)
        *freeBytes = storage.bytesAvailable();
    if (totalBytes)
        *totalBytes = storage.bytesTotal();
    return true;
}

QString FileService::errorText(const QString &error)
{
    return error.isEmpty() ? QStringLiteral("未知错误") : error;
}
