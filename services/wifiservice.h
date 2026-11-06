#ifndef WIFISERVICE_H
#define WIFISERVICE_H

#include "deviceservice.h"

#include <QString>

class CommandSequence;

// AIC8800D80。命令全部经 ProcessRunner，参数化传参，不拼 shell。
// 状态经 StatusBus 广播（状态栏 / WiFi 页都从总线取，不再各自 connect 服务）。
class WifiService : public DeviceService
{
    Q_OBJECT
public:
    static WifiService *instance();

    void initSta();                                               // wifi -o sta
    void initAp();                                                // wifi -o ap（进阶）
    void scan();                                                  // wifi -s，失败回退 iw dev wlan0 scan
    void connectTo(const QString &ssid, const QString &password);  // wifi -c ssid pass
    void disconnect();                                            // wifi -d
    void refresh() override;                                      // ip -4 addr show wlan0

    // 命令模式：把「初始化 → 连接 → 取 IP」封装成可排队的命令序列
    CommandSequence *makeConnectSequence(const QString &ssid, const QString &password);

signals:
    void scanResult(const QString &raw);

private slots:
    void onFinished(int jobId, int exitCode, int exitStatus, const QString &out);
    void onFailed(int jobId, const QString &reason);

private:
    explicit WifiService(QObject *parent = nullptr);

    int  m_scanJob;
    int  m_statusJob;
    int  m_actionJob;
    bool m_scanFallbackDone;
};

#endif // WIFISERVICE_H
