#ifndef WEBSHARESERVICE_H
#define WEBSHARESERVICE_H

#include "deviceservice.h"

#include <QStringList>

class HttpServer;
class HttpRequest;
class HttpResponse;

// 局域网网页文件管理（移植自 FongMi/TV 的「扫码远程管理」思路）
//
// 与 FongMi 的对应关系：
//   Nano(nanohttpd) → core/httpserver（QTcpServer 实现，零依赖）
//   Server.getAddress()/getPageAddress() → url() / primaryAddress()
//   端口从固定段顺延（9978–9999）→ 从 web/port 起顺延 10 个
//   网页资源放 assets → 放 Qt 资源 :/web/，随二进制走，不依赖外部文件
//
// 差异：这里没有内存里的媒体库，所以网页只做文件管理（浏览/上传/下载/改名/删除/新建目录），
// 并且所有路径限制在 FileService 的白名单根目录内（进程是 root，沙箱在服务层做死）。
class WebShareService : public DeviceService
{
    Q_OBJECT
public:
    static WebShareService *instance();

    bool start();
    void stop();
    bool isRunning() const;

    quint16 port() const;                 // 0 = 未运行
    QString token() const;
    QStringList addresses() const;        // 本机 IPv4，wlan/eth 优先
    QString primaryAddress() const;
    QString url() const;                  // http://ip:port/?k=token
    QStringList roots() const;
    int clients() const;
    QString lastRequest() const;

signals:
    void changed();

private slots:
    void onRequestLogged(const QString &line);

private:
    explicit WebShareService(QObject *parent = nullptr);

    void setupRoutes();
    void handleIndex(HttpRequest &request, HttpResponse &response);
    void handleAsset(HttpRequest &request, HttpResponse &response, const QString &resource);
    void handleRoots(HttpRequest &request, HttpResponse &response);
    void handleList(HttpRequest &request, HttpResponse &response);
    void handleDownload(HttpRequest &request, HttpResponse &response);
    void handleUpload(HttpRequest &request, HttpResponse &response);
    void handleMkdir(HttpRequest &request, HttpResponse &response);
    void handleRename(HttpRequest &request, HttpResponse &response);
    void handleDelete(HttpRequest &request, HttpResponse &response);

    HttpServer *m_server;
    quint16     m_port;
    quint16     m_portBase;
    QString     m_token;
    QString     m_lastRequest;
};

#endif // WEBSHARESERVICE_H
