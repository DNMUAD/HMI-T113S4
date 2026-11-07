#ifndef HMI_CORE_HTTPSERVER_H
#define HMI_CORE_HTTPSERVER_H

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QUrlQuery>

#include <functional>

class QTcpServer;
class QTcpSocket;
class HttpConnection;
class HttpServer;

// 极简 HTTP/1.1 服务端（QTcpServer 之上，零第三方依赖）
//
// 只做本机需要的那一小块：GET 静态资源/下载、POST 小 JSON、POST 大体积流式落盘。
// 设计约束：
//   · 全部事件驱动，任何回调里都不阻塞（大文件边读边写、按 bytesToWrite 节流）；
//   · 每个请求 Connection: close，不做 keep-alive —— 局域网里换连接的开销远小于状态机出错的代价；
//   · 请求头 16KB 上限、内联 body 256KB 上限，超过走流式接口，避免打爆 256MB 内存。

class HttpRequest
{
public:
    HttpRequest();

    QString    method;
    QString    path;                       // 已百分号解码
    QUrlQuery  query;
    QHash<QString, QString> headers;       // key 全小写
    QByteArray body;                       // 仅 contentLength <= inlineLimit 时有效
    qint64     contentLength;
    QString    peer;
    bool       streaming;                  // 大请求体：handler 需 attachBodySink()

    QString header(const QString &name) const;      // name 不区分大小写
    QString queryValue(const QString &key, const QString &fallback = QString()) const;

    // 大请求体（上传）流式接管：handler 返回后由连接层持续喂数据
    void attachBodySink(const std::function<void (const QByteArray &)> &onChunk,
                        const std::function<void (bool)> &onDone);
    bool hasBodySink() const;

    // -- 连接层内部使用 --
    std::function<void (const QByteArray &)> m_onChunk;
    std::function<void (bool)>               m_onDone;
};

class HttpResponse
{
public:
    HttpResponse();

    void setStatus(int code, const QString &reason = QString());
    void setHeader(const QString &name, const QString &value);
    void setBody(const QByteArray &data,
                 const QString &contentType = QStringLiteral("text/plain; charset=utf-8"));
    void setHtml(const QByteArray &html);
    void setJson(const QByteArray &json);
    void redirect(const QString &location);
    void setFile(const QString &filePath, const QString &contentType,
                 bool attachment, const QString &downloadName = QString());

    int     status;
    QString reason;
    QHash<QString, QString> headers;
    QByteArray body;
    bool    isFile;
    QString filePath;
    QString downloadName;
    bool    attachment;
};

class HttpServer : public QObject
{
    Q_OBJECT
public:
    typedef std::function<void (HttpRequest &, HttpResponse &)> Handler;

    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer();

    bool listen(quint16 port, const QHostAddress &address = QHostAddress::AnyIPv4);
    void close();
    bool isListening() const;
    quint16 port() const;

    void addRoute(const QString &method, const QString &path, Handler handler);
    void addPrefixRoute(const QString &method, const QString &prefix, Handler handler);

    void setToken(const QString &token);          // 非空则校验 ?k= 或 X-Token
    void setInlineBodyLimit(qint64 bytes);        // 默认 256KB
    void setMaxUploadBytes(qint64 bytes);         // 默认 4GB
    void setMaxConnections(int count);            // 默认 16

    int clientCount() const;

signals:
    void requestLogged(const QString &line);
    void clientsChanged(int count);

private:
    friend class HttpConnection;

    bool tokenValid(const HttpRequest &request) const;
    void dispatch(HttpRequest &request, HttpResponse &response) const;
    void onNewConnection();

    QTcpServer *m_server;
    QList<QPair<QString, QPair<QString, Handler> > > m_routes;   // method, (path|prefix, handler)
    QString m_token;
    qint64  m_inlineLimit;
    qint64  m_maxUpload;
    int     m_maxConnections;
    int     m_clients;
};

#endif // HMI_CORE_HTTPSERVER_H
