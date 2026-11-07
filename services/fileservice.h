#ifndef FILESERVICE_H
#define FILESERVICE_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// 网页文件管理的后端（服务层，不认识 HTTP）
//
// 进程以 root 运行，所以「路径沙箱」必须在这一层做死：
//   1. 只允许访问 web/roots 配置的白名单目录（默认 /mnt/usb、/media、/usr/share、/tmp）；
//   2. 一律用 canonical 路径做包含判断，符号链接指到白名单外同样被拒；
//   3. 新建/上传的目标路径要用「父目录 canonical + 文件名」再校验一次；
//   4. 白名单根目录本身不允许删除或改名。
class FileService : public QObject
{
    Q_OBJECT
public:
    struct Entry
    {
        QString name;
        QString path;
        bool    isDir;
        qint64  size;
        qint64  mtime;

        Entry() : isDir(false), size(0), mtime(0) {}
    };

    static FileService *instance();

    void setRoots(const QStringList &roots);
    QStringList roots() const;
    bool isRoot(const QString &path) const;

    // 合法则返回 canonical 绝对路径，否则返回空
    QString canonical(const QString &path) const;

    bool list(const QString &path, QList<Entry> *entries, QString *error) const;
    bool mkdir(const QString &parent, const QString &name, QString *error);
    bool renameItem(const QString &path, const QString &newName, QString *error);
    bool removeItem(const QString &path, bool recursive, QString *error);
    bool usage(const QString &path, qint64 *freeBytes, qint64 *totalBytes) const;

    // 只允许单层文件名：非空、不含路径分隔符、不是 . / ..
    static bool validName(const QString &name);
    static QString errorText(const QString &error);

private:
    explicit FileService(QObject *parent = nullptr);

    QStringList m_roots;      // canonical 形式
};

#endif // FILESERVICE_H
