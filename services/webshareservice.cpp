#include "webshareservice.h"
#include "appsettings.h"
#include "fileservice.h"
#include "httpserver.h"
#include "logpanel.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QSharedPointer>

namespace {

// 默认可访问目录：板端是挂载点，Windows 桌面版是用户目录 + 临时目录。
// 生产环境请勿把根目录加进来——本程序以 root 运行。
QString defaultWebRootsSpec()
{
#ifdef Q_OS_WIN
    QStringList roots = QStringList() << QDir::homePath() << QDir::tempPath();
    roots.removeDuplicates();
    return roots.join(QLatin1Char(','));
#else
    return QStringLiteral("/mnt/usb,/media,/usr/share,/tmp");
#endif
}

QByteArray jsonError(const QString &message)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("ok"), false);
    obj.insert(QStringLiteral("error"), message);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray jsonOk(const QJsonObject &extra = QJsonObject())
{
    QJsonObject obj = extra;
    obj.insert(QStringLiteral("ok"), true);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QString parentOf(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QString();
    return path.left(slash);
}

QByteArray readResource(const QString &resource)
{
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

} // namespace

WebShareService *WebShareService::instance()
{
    return hmi::singleton<WebShareService>([]() { return new WebShareService; });
}

WebShareService::WebShareService(QObject *parent)
    : DeviceService(QStringLiteral("web"), QObject::tr("网页管理"), 0, parent)
    , m_server(nullptr)
    , m_port(0)
    , m_portBase(8080)
{
    AppSettings *settings = AppSettings::instance();
    m_portBase = static_cast<quint16>(qBound(1024, settings->value(QStringLiteral("web/port"),
                                                                  QStringLiteral("8080")).toInt(), 65000));
    m_token = settings->value(QStringLiteral("web/token")).trimmed();

    QStringList roots = settings->value(QStringLiteral("web/roots"), defaultWebRootsSpec())
                            .split(QLatin1Char(','), QString::SkipEmptyParts);
    for (int i = 0; i < roots.size(); ++i)
        roots[i] = roots.at(i).trimmed();
    FileService::instance()->setRoots(roots);

    // 桌面调试（或 U 盘未插）时白名单可能一个都不存在，退化成临时目录，保证功能可验证
    if (FileService::instance()->roots().isEmpty()) {
        const QString fallback = QFileInfo(QDir::tempPath()).canonicalFilePath();
        if (!fallback.isEmpty()) {
            FileService::instance()->setRoots(QStringList() << fallback);
            LogPanel::post(tr("web/roots 无可用目录，临时退化为 %1").arg(fallback), LogPanel::Warn);
        }
    }
}

bool WebShareService::isRunning() const
{
    return m_server && m_server->isListening();
}

quint16 WebShareService::port() const
{
    return isRunning() ? m_port : 0;
}

QString WebShareService::token() const
{
    return m_token;
}

QStringList WebShareService::roots() const
{
    return FileService::instance()->roots();
}

int WebShareService::clients() const
{
    return m_server ? m_server->clientCount() : 0;
}

QString WebShareService::lastRequest() const
{
    return m_lastRequest;
}

QStringList WebShareService::addresses() const
{
    QStringList wifi;
    QStringList wired;
    QStringList other;

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (int i = 0; i < interfaces.size(); ++i) {
        const QNetworkInterface &iface = interfaces.at(i);
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning))
            continue;
        if (flags & QNetworkInterface::IsLoopBack)
            continue;

        const QString name = iface.name();
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (int j = 0; j < entries.size(); ++j) {
            const QHostAddress ip = entries.at(j).ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback())
                continue;
            const QString text = ip.toString();
            if (text.startsWith(QStringLiteral("169.254.")))
                continue;               // 链路本地地址，手机连不上
            if (name.startsWith(QStringLiteral("wlan")))
                wifi << text;
            else if (name.startsWith(QStringLiteral("eth")))
                wired << text;
            else
                other << text;
        }
    }

    return wifi + wired + other;
}

QString WebShareService::primaryAddress() const
{
    const QStringList list = addresses();
    return list.isEmpty() ? QString() : list.first();
}

QString WebShareService::url() const
{
    if (!isRunning())
        return QString();
    const QString ip = primaryAddress();
    if (ip.isEmpty())
        return QString();
    QString link = QStringLiteral("http://%1:%2/").arg(ip).arg(m_port);
    if (!m_token.isEmpty())
        link += QStringLiteral("?k=") + m_token;
    return link;
}

bool WebShareService::start()
{
    if (isRunning())
        return true;

    if (!m_server) {
        m_server = new HttpServer(this);
        setupRoutes();
        connect(m_server, &HttpServer::requestLogged, this, &WebShareService::onRequestLogged);
        connect(m_server, &HttpServer::clientsChanged, this, [this](int) { emit changed(); });
    }

    m_server->setToken(m_token);

    for (int offset = 0; offset < 10; ++offset) {
        const quint16 candidate = static_cast<quint16>(m_portBase + offset);
        if (m_server->listen(candidate)) {
            m_port = candidate;
            LogPanel::post(tr("网页文件管理已启动：%1（根目录 %2）")
                           .arg(url(), roots().join(QStringLiteral(" "))), LogPanel::Ok);
            publish(DeviceReady, url());
            emit changed();
            return true;
        }
    }

    m_port = 0;
    publish(DeviceError, tr("端口 %1–%2 均被占用").arg(m_portBase).arg(m_portBase + 9));
    emit changed();
    return false;
}

void WebShareService::stop()
{
    if (!m_server || !m_server->isListening())
        return;
    m_server->close();
    m_port = 0;
    publish(DeviceOff, tr("已停止"));
    LogPanel::post(tr("网页文件管理已停止"), LogPanel::Info);
    emit changed();
}

void WebShareService::onRequestLogged(const QString &line)
{
    m_lastRequest = line;
    LogPanel::post(QStringLiteral("[web] ") + line, LogPanel::Debug);
    emit changed();
}

void WebShareService::setupRoutes()
{
    m_server->addRoute(QStringLiteral("GET"), QStringLiteral("/"),
                       [this](HttpRequest &q, HttpResponse &s) { handleIndex(q, s); });
    m_server->addRoute(QStringLiteral("GET"), QStringLiteral("/index.html"),
                       [this](HttpRequest &q, HttpResponse &s) { handleIndex(q, s); });
    m_server->addRoute(QStringLiteral("GET"), QStringLiteral("/app.css"),
                       [this](HttpRequest &q, HttpResponse &s) { handleAsset(q, s, QStringLiteral(":/web/app.css")); });
    m_server->addRoute(QStringLiteral("GET"), QStringLiteral("/app.js"),
                       [this](HttpRequest &q, HttpResponse &s) { handleAsset(q, s, QStringLiteral(":/web/app.js")); });

    m_server->addRoute(QStringLiteral("GET"), QStringLiteral("/api/roots"),
                       [this](HttpRequest &q, HttpResponse &s) { handleRoots(q, s); });
    m_server->addRoute(QStringLiteral("GET"), QStringLiteral("/api/list"),
                       [this](HttpRequest &q, HttpResponse &s) { handleList(q, s); });
    m_server->addRoute(QStringLiteral("GET"), QStringLiteral("/api/download"),
                       [this](HttpRequest &q, HttpResponse &s) { handleDownload(q, s); });

    m_server->addRoute(QStringLiteral("POST"), QStringLiteral("/api/upload"),
                       [this](HttpRequest &q, HttpResponse &s) { handleUpload(q, s); });
    m_server->addRoute(QStringLiteral("POST"), QStringLiteral("/api/mkdir"),
                       [this](HttpRequest &q, HttpResponse &s) { handleMkdir(q, s); });
    m_server->addRoute(QStringLiteral("POST"), QStringLiteral("/api/rename"),
                       [this](HttpRequest &q, HttpResponse &s) { handleRename(q, s); });
    m_server->addRoute(QStringLiteral("POST"), QStringLiteral("/api/delete"),
                       [this](HttpRequest &q, HttpResponse &s) { handleDelete(q, s); });
}

void WebShareService::handleIndex(HttpRequest &, HttpResponse &response)
{
    const QByteArray html = readResource(QStringLiteral(":/web/index.html"));
    if (html.isEmpty()) {
        response.setStatus(500);
        response.setHtml("<html><meta charset=\"utf-8\"><body>缺少内嵌网页资源（:/web/index.html）</body></html>");
        return;
    }
    response.setHtml(html);
}

void WebShareService::handleAsset(HttpRequest &, HttpResponse &response, const QString &resource)
{
    const QByteArray data = readResource(resource);
    if (data.isEmpty()) {
        response.setStatus(404);
        response.setBody("missing asset");
        return;
    }
    const QString type = resource.endsWith(QStringLiteral(".css"))
                             ? QStringLiteral("text/css; charset=utf-8")
                             : QStringLiteral("application/javascript; charset=utf-8");
    response.setBody(data, type);
}

void WebShareService::handleRoots(HttpRequest &, HttpResponse &response)
{
    FileService *files = FileService::instance();
    const QStringList roots = files->roots();

    QJsonArray array;
    for (int i = 0; i < roots.size(); ++i) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), QFileInfo(roots.at(i)).fileName());
        item.insert(QStringLiteral("path"), roots.at(i));
        array.append(item);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("roots"), array);
    obj.insert(QStringLiteral("device"), QFileInfo(QStringLiteral("/proc/device-tree/model")).exists()
                                         ? QStringLiteral("T113-S4") : QStringLiteral("HMI"));
    response.setJson(jsonOk(obj));
}

void WebShareService::handleList(HttpRequest &request, HttpResponse &response)
{
    const QString path = request.queryValue(QStringLiteral("path"));
    FileService *files = FileService::instance();

    const QString dir = files->canonical(path);
    if (dir.isEmpty()) {
        response.setStatus(403);
        response.setJson(jsonError(tr("路径不在允许范围内")));
        return;
    }

    QList<FileService::Entry> entries;
    QString error;
    if (!files->list(dir, &entries, &error)) {
        response.setStatus(404);
        response.setJson(jsonError(error));
        return;
    }

    QJsonArray items;
    for (int i = 0; i < entries.size(); ++i) {
        const FileService::Entry &entry = entries.at(i);
        QJsonObject item;
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("path"), entry.path);
        item.insert(QStringLiteral("dir"), entry.isDir);
        item.insert(QStringLiteral("size"), static_cast<double>(entry.size));
        item.insert(QStringLiteral("mtime"), static_cast<double>(entry.mtime));
        items.append(item);
    }

    const QString parent = parentOf(dir);
    QJsonObject obj;
    obj.insert(QStringLiteral("path"), dir);
    obj.insert(QStringLiteral("parent"), files->canonical(parent).isEmpty() ? QString() : parent);
    obj.insert(QStringLiteral("isRoot"), files->isRoot(dir));
    obj.insert(QStringLiteral("items"), items);

    qint64 freeBytes = 0;
    qint64 totalBytes = 0;
    if (files->usage(dir, &freeBytes, &totalBytes)) {
        obj.insert(QStringLiteral("free"), static_cast<double>(freeBytes));
        obj.insert(QStringLiteral("total"), static_cast<double>(totalBytes));
    }
    response.setJson(jsonOk(obj));
}

void WebShareService::handleDownload(HttpRequest &request, HttpResponse &response)
{
    const QString path = FileService::instance()->canonical(request.queryValue(QStringLiteral("path")));
    if (path.isEmpty()) {
        response.setStatus(403);
        response.setJson(jsonError(tr("路径不在允许范围内")));
        return;
    }
    const QFileInfo info(path);
    if (!info.exists() || info.isDir()) {
        response.setStatus(404);
        response.setJson(jsonError(tr("文件不存在")));
        return;
    }
    response.setFile(path, QString(), true, info.fileName());
}

void WebShareService::handleUpload(HttpRequest &request, HttpResponse &response)
{
    FileService *files = FileService::instance();
    const QString dir = files->canonical(request.queryValue(QStringLiteral("path")));
    const QString name = request.queryValue(QStringLiteral("name")).trimmed();

    if (dir.isEmpty()) {
        response.setStatus(403);
        response.setJson(jsonError(tr("目标目录不在允许范围内")));
        return;
    }
    if (!FileService::validName(name)) {
        response.setStatus(400);
        response.setJson(jsonError(tr("文件名不合法")));
        return;
    }

    const QString target = files->canonical(dir + QLatin1Char('/') + name);
    if (target.isEmpty()) {
        response.setStatus(403);
        response.setJson(jsonError(tr("目标路径不合法")));
        return;
    }
    if (QFileInfo(target).isDir()) {
        response.setStatus(409);
        response.setJson(jsonError(tr("同名目录已存在")));
        return;
    }

    if (request.contentLength <= 0) {
        response.setStatus(400);
        response.setJson(jsonError(tr("空文件或缺少 Content-Length")));
        return;
    }

    QSharedPointer<QFile> file(new QFile(target));
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        response.setStatus(500);
        response.setJson(jsonError(tr("无法写入 %1：%2").arg(name, file->errorString())));
        return;
    }

    QSharedPointer<qint64> written(new qint64(0));
    QSharedPointer<QString> failure(new QString());
    const QString shownName = name;

    request.attachBodySink(
        [file, written, failure](const QByteArray &chunk) {
            if (!failure->isEmpty())
                return;
            if (file->write(chunk) != chunk.size())
                *failure = file->errorString();
            *written += chunk.size();
        },
        [this, file, written, failure, shownName, &response](bool ok) {
            file->flush();
            file->close();
            if (!ok && failure->isEmpty())
                *failure = tr("传输中断");

            if (!failure->isEmpty()) {
                QFile::remove(file->fileName());
                response.setStatus(500);
                response.setJson(jsonError(tr("上传失败：%1").arg(*failure)));
                return;
            }

            QJsonObject obj;
            obj.insert(QStringLiteral("name"), shownName);
            obj.insert(QStringLiteral("size"), static_cast<double>(*written));
            response.setJson(jsonOk(obj));
            emit changed();
        });
}

void WebShareService::handleMkdir(HttpRequest &request, HttpResponse &response)
{
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QString error;
    if (!FileService::instance()->mkdir(body.value(QStringLiteral("path")).toString(),
                                        body.value(QStringLiteral("name")).toString(),
                                        &error)) {
        response.setStatus(400);
        response.setJson(jsonError(error));
        return;
    }
    response.setJson(jsonOk());
}

void WebShareService::handleRename(HttpRequest &request, HttpResponse &response)
{
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QString error;
    if (!FileService::instance()->renameItem(body.value(QStringLiteral("path")).toString(),
                                             body.value(QStringLiteral("name")).toString(),
                                             &error)) {
        response.setStatus(400);
        response.setJson(jsonError(error));
        return;
    }
    response.setJson(jsonOk());
}

void WebShareService::handleDelete(HttpRequest &request, HttpResponse &response)
{
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    const bool recursive = body.value(QStringLiteral("recursive")).toBool(false);
    QString error;
    if (!FileService::instance()->removeItem(body.value(QStringLiteral("path")).toString(),
                                             recursive, &error)) {
        response.setStatus(400);
        response.setJson(jsonError(error));
        return;
    }
    response.setJson(jsonOk());
}

HMI_REGISTER_SERVICE("web", WebShareService)
