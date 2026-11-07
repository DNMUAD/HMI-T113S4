#include "httpserver.h"

#include <QFile>
#include <QFileInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace {

const int    kMaxHeaderBytes = 16 * 1024;
const qint64 kFileChunk      = 64 * 1024;
const qint64 kWriteHighWater = 256 * 1024;

QString reasonPhrase(int code)
{
    switch (code) {
    case 200: return QStringLiteral("OK");
    case 302: return QStringLiteral("Found");
    case 400: return QStringLiteral("Bad Request");
    case 403: return QStringLiteral("Forbidden");
    case 404: return QStringLiteral("Not Found");
    case 409: return QStringLiteral("Conflict");
    case 413: return QStringLiteral("Payload Too Large");
    case 415: return QStringLiteral("Unsupported Media Type");
    case 431: return QStringLiteral("Request Header Fields Too Large");
    case 500: return QStringLiteral("Internal Server Error");
    case 503: return QStringLiteral("Service Unavailable");
    default:  return QStringLiteral("OK");
    }
}

QString contentTypeFor(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        return QStringLiteral("text/html; charset=utf-8");
    if (suffix == QLatin1String("css"))  return QStringLiteral("text/css; charset=utf-8");
    if (suffix == QLatin1String("js"))   return QStringLiteral("application/javascript; charset=utf-8");
    if (suffix == QLatin1String("json")) return QStringLiteral("application/json; charset=utf-8");
    if (suffix == QLatin1String("png"))  return QStringLiteral("image/png");
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (suffix == QLatin1String("svg"))  return QStringLiteral("image/svg+xml");
    if (suffix == QLatin1String("txt") || suffix == QLatin1String("log"))
        return QStringLiteral("text/plain; charset=utf-8");
    if (suffix == QLatin1String("mp4"))  return QStringLiteral("video/mp4");
    if (suffix == QLatin1String("mp3"))  return QStringLiteral("audio/mpeg");
    if (suffix == QLatin1String("wav"))  return QStringLiteral("audio/wav");
    return QStringLiteral("application/octet-stream");
}

} // namespace

// ---------------------------------------------------------------- HttpRequest

HttpRequest::HttpRequest()
    : contentLength(0)
    , streaming(false)
{
}

QString HttpRequest::header(const QString &name) const
{
    return headers.value(name.toLower());
}

QString HttpRequest::queryValue(const QString &key, const QString &fallback) const
{
    if (!query.hasQueryItem(key))
        return fallback;
    return query.queryItemValue(key, QUrl::FullyDecoded);
}

void HttpRequest::attachBodySink(const std::function<void (const QByteArray &)> &onChunk,
                                 const std::function<void (bool)> &onDone)
{
    m_onChunk = onChunk;
    m_onDone = onDone;

    // 小请求体（未超过 inlineLimit）不走流式：dispatch 之前 body 已整体收齐，
    // 不会再有 chunk 回调。这里同步把整块喂给 sink 并立即收尾，
    // 否则上传接口在 inlineLimit 以下会得到一个 0 字节空文件。
    if (!streaming) {
        const QByteArray whole = body;
        body.clear();
        if (m_onChunk && !whole.isEmpty())
            m_onChunk(whole);
        if (m_onDone) {
            const std::function<void (bool)> done = m_onDone;
            m_onChunk = nullptr;
            m_onDone = nullptr;
            done(true);
        }
    }
}

bool HttpRequest::hasBodySink() const
{
    return m_onChunk != nullptr || m_onDone != nullptr;
}

// --------------------------------------------------------------- HttpResponse

HttpResponse::HttpResponse()
    : status(0)
    , isFile(false)
    , attachment(false)
{
}

void HttpResponse::setStatus(int code, const QString &reason)
{
    status = code;
    this->reason = reason.isEmpty() ? reasonPhrase(code) : reason;
}

void HttpResponse::setHeader(const QString &name, const QString &value)
{
    headers.insert(name, value);
}

void HttpResponse::setBody(const QByteArray &data, const QString &contentType)
{
    body = data;
    headers.insert(QStringLiteral("Content-Type"), contentType);
}

void HttpResponse::setHtml(const QByteArray &html)
{
    setBody(html, QStringLiteral("text/html; charset=utf-8"));
}

void HttpResponse::setJson(const QByteArray &json)
{
    setBody(json, QStringLiteral("application/json; charset=utf-8"));
}

void HttpResponse::redirect(const QString &location)
{
    status = 302;
    reason = reasonPhrase(302);
    headers.insert(QStringLiteral("Location"), location);
    body.clear();
}

void HttpResponse::setFile(const QString &path, const QString &contentType,
                           bool asAttachment, const QString &downloadName)
{
    isFile = true;
    filePath = path;
    this->downloadName = downloadName;   // 形参与成员同名，必须显式 this->
    attachment = asAttachment;
    if (!contentType.isEmpty())
        headers.insert(QStringLiteral("Content-Type"), contentType);
}

// ------------------------------------------------------------- HttpConnection

class HttpConnection : public QObject
{
    Q_OBJECT
public:
    HttpConnection(QTcpSocket *socket, HttpServer *server)
        : QObject(server)
        , m_socket(socket)
        , m_server(server)
        , m_headersParsed(false)
        , m_bodyReceived(0)
        , m_done(false)
        , m_file(nullptr)
    {
        m_socket->setParent(this);
        connect(m_socket, &QTcpSocket::readyRead, this, &HttpConnection::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, this, &HttpConnection::onDisconnected);
        connect(m_socket, &QTcpSocket::bytesWritten, this, &HttpConnection::onBytesWritten);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        connect(m_socket, &QAbstractSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) { onSocketError(); });
#else
        connect(m_socket, SIGNAL(error(QAbstractSocket::SocketError)),
                this, SLOT(onSocketError()));
#endif
    }

    ~HttpConnection()
    {
        if (m_file) {
            m_file->close();
            delete m_file;
            m_file = nullptr;
        }
    }

private slots:
    void onReadyRead()
    {
        if (m_done && !m_file)
            return;
        m_buffer.append(m_socket->readAll());

        if (!m_headersParsed) {
            if (!parseHeaders())
                return;
            if (m_done)
                return;
        }

        if (m_req.streaming) {
            feedStream();
            return;
        }

        if (m_dispatched)
            return;

        if (static_cast<qint64>(m_buffer.size()) < m_req.contentLength)
            return;                       // 等请求体收齐（内联上限内，最大 256KB）

        m_req.body = m_buffer.left(static_cast<int>(m_req.contentLength));
        m_buffer.remove(0, static_cast<int>(m_req.contentLength));
        m_dispatched = true;
        m_server->dispatch(m_req, m_res);
        sendResponse();
    }

    void onBytesWritten(qint64)
    {
        pumpFile();
    }

    void onDisconnected()
    {
        deleteLater();
    }

    void onSocketError()
    {
        if (!m_done)
            deleteLater();
    }

private:
    bool parseHeaders()
    {
        const int end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (m_buffer.size() > kMaxHeaderBytes)
                fail(431, QStringLiteral("请求头过大"));
            return false;
        }

        const QByteArray head = m_buffer.left(end);
        m_buffer.remove(0, end + 4);

        const QList<QByteArray> lines = head.split('\n');
        if (lines.isEmpty()) {
            fail(400, QStringLiteral("请求行缺失"));
            return false;
        }

        const QList<QByteArray> parts = lines.first().trimmed().split(' ');
        if (parts.size() < 2) {
            fail(400, QStringLiteral("请求行格式错误"));
            return false;
        }

        m_req.method = QString::fromLatin1(parts.at(0)).trimmed().toUpper();
        const QByteArray target = parts.at(1).trimmed();
        const int q = target.indexOf('?');
        m_req.path = QUrl::fromPercentEncoding(q >= 0 ? target.left(q) : target);
        if (q >= 0)
            m_req.query = QUrlQuery(QString::fromLatin1(target.mid(q + 1)));

        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            m_req.headers.insert(QString::fromLatin1(line.left(colon)).trimmed().toLower(),
                                 QString::fromUtf8(line.mid(colon + 1)).trimmed());
        }

        m_req.peer = m_socket->peerAddress().toString();
        m_req.contentLength = m_req.header(QStringLiteral("content-length")).toLongLong();
        if (m_req.contentLength < 0)
            m_req.contentLength = 0;

        if (m_req.contentLength > m_server->m_maxUpload) {
            fail(413, QStringLiteral("请求体超过上限"));
            return false;
        }

        if (!m_server->tokenValid(m_req)) {
            m_res.setStatus(403);
            m_res.setHtml("<html><meta charset=\"utf-8\"><body style=\"font-family:sans-serif;padding:24px\">"
                          "<h3>需要访问令牌</h3><p>请扫描设备屏幕上的二维码打开本页。</p></body></html>");
            m_done = true;
            sendResponse();
            return false;
        }

        m_headersParsed = true;
        m_req.streaming = (m_req.contentLength > m_server->m_inlineLimit);

        if (m_req.streaming) {
            m_server->dispatch(m_req, m_res);
            m_dispatched = true;
            if (!m_req.hasBodySink()) {
                // handler 已经给出错误响应（如 403/400）时保留它，否则按接口误用处理
                if (m_res.status == 0) {
                    m_res.setStatus(400);
                    m_res.setJson(QByteArray("{\"ok\":false,\"error\":\"该接口需要流式接收请求体\"}"));
                }
                m_done = true;
                sendResponse();
                return false;
            }
        }
        return true;
    }

    void feedStream()
    {
        while (!m_done && m_bodyReceived < m_req.contentLength && !m_buffer.isEmpty()) {
            const qint64 want = qMin<qint64>(m_buffer.size(), m_req.contentLength - m_bodyReceived);
            const QByteArray chunk = m_buffer.left(static_cast<int>(want));
            m_buffer.remove(0, static_cast<int>(want));
            m_bodyReceived += want;
            if (m_req.m_onChunk)
                m_req.m_onChunk(chunk);
        }
        if (m_bodyReceived >= m_req.contentLength && !m_done) {
            if (m_req.m_onDone)
                m_req.m_onDone(true);
            sendResponse();
        }
    }

    void fail(int code, const QString &message)
    {
        if (m_done)
            return;
        m_res.setStatus(code);
        m_res.setJson(QStringLiteral("{\"ok\":false,\"error\":\"%1\"}").arg(message).toUtf8());
        m_done = true;
        sendResponse();
    }

    void sendResponse()
    {
        if (m_sent)
            return;
        m_sent = true;

        if (m_res.status == 0)
            m_res.setStatus(200);

        QHash<QString, QString> headers = m_res.headers;
        headers.insert(QStringLiteral("Server"), QStringLiteral("HMI-T113S4"));
        headers.insert(QStringLiteral("Connection"), QStringLiteral("close"));
        headers.insert(QStringLiteral("Cache-Control"), QStringLiteral("no-store"));

        if (m_res.isFile) {
            const QFileInfo info(m_res.filePath);
            headers.insert(QStringLiteral("Content-Length"), QString::number(info.size()));
            if (!headers.contains(QStringLiteral("Content-Type")))
                headers.insert(QStringLiteral("Content-Type"), contentTypeFor(m_res.filePath));
            if (m_res.attachment) {
                const QString name = m_res.downloadName.isEmpty() ? info.fileName() : m_res.downloadName;
                headers.insert(QStringLiteral("Content-Disposition"),
                               QStringLiteral("attachment; filename*=UTF-8''%1")
                               .arg(QString::fromLatin1(QUrl::toPercentEncoding(name))));
            }
        } else {
            headers.insert(QStringLiteral("Content-Length"), QString::number(m_res.body.size()));
            if (!headers.contains(QStringLiteral("Content-Type")))
                headers.insert(QStringLiteral("Content-Type"),
                               QStringLiteral("text/plain; charset=utf-8"));
        }

        QByteArray head;
        head += "HTTP/1.1 " + QByteArray::number(m_res.status) + ' '
                + m_res.reason.toUtf8() + "\r\n";
        QHash<QString, QString>::ConstIterator it = headers.constBegin();
        for (; it != headers.constEnd(); ++it)
            head += it.key().toUtf8() + ": " + it.value().toUtf8() + "\r\n";
        head += "\r\n";

        m_socket->write(head);

        QString status = QString::number(m_res.status);
        emit m_server->requestLogged(QStringLiteral("%1 %2 ← %3 → %4")
                                     .arg(m_req.method, m_req.path, m_req.peer, status));

        if (m_res.isFile) {
            m_file = new QFile(m_res.filePath);
            if (!m_file->open(QIODevice::ReadOnly)) {
                delete m_file;
                m_file = nullptr;
                m_socket->disconnectFromHost();
                return;
            }
            pumpFile();
            return;
        }

        if (!m_res.body.isEmpty())
            m_socket->write(m_res.body);
        m_socket->disconnectFromHost();
    }

    void pumpFile()
    {
        if (!m_file)
            return;
        while (!m_file->atEnd() && m_socket->bytesToWrite() < kWriteHighWater) {
            const QByteArray chunk = m_file->read(kFileChunk);
            if (chunk.isEmpty())
                break;
            m_socket->write(chunk);
        }
        if (m_file->atEnd() && m_socket->bytesToWrite() == 0) {
            m_file->close();
            delete m_file;
            m_file = nullptr;
            m_socket->disconnectFromHost();
        }
    }

    QTcpSocket  *m_socket;
    HttpServer  *m_server;
    QByteArray   m_buffer;
    HttpRequest  m_req;
    HttpResponse m_res;
    bool         m_headersParsed;
    bool         m_dispatched = false;
    bool         m_sent = false;
    bool         m_done;
    qint64       m_bodyReceived;
    QFile       *m_file;
};

// ---------------------------------------------------------------- HttpServer

HttpServer::HttpServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_inlineLimit(256 * 1024)
    , m_maxUpload(4LL * 1024 * 1024 * 1024)
    , m_maxConnections(16)
    , m_clients(0)
{
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

HttpServer::~HttpServer()
{
    close();
}

bool HttpServer::listen(quint16 port, const QHostAddress &address)
{
    if (m_server->isListening())
        return true;
    return m_server->listen(address, port);
}

void HttpServer::close()
{
    if (m_server->isListening())
        m_server->close();
}

bool HttpServer::isListening() const
{
    return m_server->isListening();
}

quint16 HttpServer::port() const
{
    return m_server->serverPort();
}

void HttpServer::addRoute(const QString &method, const QString &path, Handler handler)
{
    m_routes.append(qMakePair(method.toUpper(), qMakePair(path, handler)));
}

void HttpServer::addPrefixRoute(const QString &method, const QString &prefix, Handler handler)
{
    m_routes.append(qMakePair(method.toUpper(), qMakePair(prefix + QLatin1Char('*'), handler)));
}

void HttpServer::setToken(const QString &token)
{
    m_token = token;
}

void HttpServer::setInlineBodyLimit(qint64 bytes)
{
    m_inlineLimit = qBound<qint64>(1024, bytes, 16LL * 1024 * 1024);
}

void HttpServer::setMaxUploadBytes(qint64 bytes)
{
    m_maxUpload = qBound<qint64>(1024 * 1024, bytes, 64LL * 1024 * 1024 * 1024);
}

void HttpServer::setMaxConnections(int count)
{
    m_maxConnections = qBound(1, count, 64);
}

int HttpServer::clientCount() const
{
    return m_clients;
}

bool HttpServer::tokenValid(const HttpRequest &request) const
{
    if (m_token.isEmpty())
        return true;
    if (request.path.startsWith(QStringLiteral("/favicon")))
        return true;
    if (request.queryValue(QStringLiteral("k")) == m_token)
        return true;
    if (request.header(QStringLiteral("x-token")) == m_token)
        return true;
    return false;
}

void HttpServer::dispatch(HttpRequest &request, HttpResponse &response) const
{
    for (int i = 0; i < m_routes.size(); ++i) {
        const QString &method = m_routes.at(i).first;
        if (method != request.method)
            continue;
        const QString &pattern = m_routes.at(i).second.first;
        if (pattern.endsWith(QLatin1Char('*'))) {
            const QString prefix = pattern.left(pattern.size() - 1);
            if (!request.path.startsWith(prefix))
                continue;
        } else if (pattern != request.path) {
            continue;
        }
        m_routes.at(i).second.second(request, response);
        return;
    }
    response.setStatus(404);
    response.setJson(QByteArray("{\"ok\":false,\"error\":\"not found\"}"));
}

void HttpServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (m_clients >= m_maxConnections) {
            socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        ++m_clients;
        emit clientsChanged(m_clients);

        HttpConnection *connection = new HttpConnection(socket, this);
        connect(connection, &QObject::destroyed, this, [this]() {
            --m_clients;
            emit clientsChanged(m_clients);
        });
    }
}

#include "httpserver.moc"
