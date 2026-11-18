#ifndef WIFISERVICE_H
#define WIFISERVICE_H

#include "deviceservice.h"

#include <QString>
#include <QStringList>
class CommandSequence;
class QTimer;

// AP 模式下的一台已连接设备。
// IP 可能为空：`wifi -l ap` 只给 MAC，IP 是从 /proc/net/arp 反查的，
// 设备刚连上、或没走 DHCP 时 ARP 表里还没有它。
struct ApClient
{
    QString mac;
    QString ip;
};

// AIC8800D80 + wifimanager（wifi_daemon）的命令封装。
// 命令全部经 ProcessRunner，参数化传参，不拼 shell；状态经 StatusBus 广播。
//
// ── 链路分工 ─────────────────────────────────────────────────────
// 开机阶段由 /etc/init.d/S41wifi 完成（驱动 + wlan0 + wifi_daemon）。
// 本服务只负责开机之后：模式切换、扫描、连接、AP 热点。
//
// ── 两种模式 ─────────────────────────────────────────────────────
//   STA：连上游热点。切换 = `wifi -o sta`，扫描走 wpa_cli。
//   AP ：自家发热点。切换 = `wifi -f sta`（按 wifimanager 的用法，
//        必须先关 STA 才能开 AP），开热点 = `wifi -o ap ...`。
//
// ── 为什么扫描不用 `wifi -s` ─────────────────────────────────────
// 这个客户端只把命令写进 /tmp/UNIX_WIFI.domain 就退出，二进制里连
// read/recv 符号都没有（readelf 确认只有 socket/connect/write），扫描结果
// 由 wifi_daemon 打到它自己的 stdout —— 应用读不到。所以扫描走
// wpa_cli（连 wpa_supplicant 控制 socket，结果 printf 到 stdout），
// 与 daemon 内部用的是同一个 supplicant，数据一致。
class WifiService : public DeviceService
{
    Q_OBJECT
public:
    enum WifiMode
    {
        ModeSta = 0,
        ModeAp
    };
    Q_ENUM(WifiMode)

    static WifiService *instance();

    bool driverReady() const;      // wlan0 是否存在
    bool daemonReady() const;      // wifi_daemon 是否在运行
    bool linkReady() const;        // 两者都就绪

    WifiMode mode() const;
    // 切换模式只改内部状态并发出 modeChanged，不执行任何命令 ——
    // 命令由 enterMode()/页面在合适时机触发，避免「切模式」和「开热点」重复执行。
    void setMode(WifiMode mode);

    // 按当前模式开启：STA → `wifi -o sta`；AP → `wifi -f sta`（关掉 STA 让位）。
    // 链路没起来时先补跑 S41wifi start。
    void enterMode();

    // 重启 WiFi：S41wifi stop → start → enterMode（整条走 CommandSequence 排队）
    void restartLink();

    // STA
    void scan();                   // 触发扫描 + 倒计时，倒计时结束后取结果
    void stopScan();               // 立即终止本轮扫描（离开页面时调用）
    void connectTo(const QString &ssid, const QString &password);
    void disconnect();
    CommandSequence *makeConnectSequence(const QString &ssid, const QString &password);

    // AP
    void openAp(const QString &ssid, const QString &password,
                int channel, const QString &security);
    void listApClients();          // wifi -l ap
    void closeAp();                // wifi -f ap

    // 把配置里的 wifi/logLevel 下发给驱动（wifi -D <level>）。
    //
    // 只影响驱动自己的内核打印（AICWFDBG 那堆 TRACE/DEBUG），
    // 不影响 wifi / wpa_cli 返回的数据，所以扫描、连接结果与等级无关。
    // 幂等：同一个等级只下发一次，避免每次进页面都重跑一条命令。
    void applyLogLevel();

    void refresh() override;       // ip -4 addr show <wlan>

signals:
    void scanResult(const QString &raw);
    // 扫描被拒绝（链路未就绪 / AP 模式 / 正在切 STA）。
    //
    // 必须有一条这样的信号：页面在点「扫描」时就把按钮置灰了，
    // 如果服务因为前置条件不满足直接 return，就不会有 scanResult
    // 或 scanCountdown 回来，按钮永远恢复不了 —— 用户看到的就是
    // 「扫描按钮一直是灰的」。
    void scanRefused(const QString &reason);
    // 扫描倒计时：>0 剩余秒数；0 表示倒计时结束；-1 表示本轮被取消
    void scanCountdown(int seconds);
    void apClients(const QList<ApClient> &clients);   // wifi -l ap + ARP 反查出的 IP
    void apClientsFailed(const QString &reason);
    void modeChanged(WifiService::WifiMode mode);
    void apOpened(bool ok, const QString &detail);

private slots:
    void onFinished(int jobId, int exitCode, int exitStatus, const QString &out);
    void onFailed(int jobId, const QString &reason);
    void onScanTick();

private:
    explicit WifiService(QObject *parent = nullptr);

    // 正在执行的单个动作。用状态机而不是给每个 job 一个成员：
    // jobId 只在提交时确定，回调里靠 jobId 比对分派，动作种类必须记下来。
    enum Action
    {
        ActNone = 0,
        ActLinkStart,       // 补跑 S41wifi start
        ActEnterSta,        // wifi -o sta
        ActEnterFSta,       // wifi -f sta（进 AP 前让位）
        ActOpenAp,          // wifi -o ap ...
        ActCloseAp,         // wifi -f ap
        ActListAp,          // wifi -l ap
        ActConnect,         // wifi -c ssid pass
        ActDisconnect,      // wifi -d
        ActScanTrigger,     // wpa_cli scan
        ActScanResults      // wpa_cli scan_results
    };

    void detectInterfaces();
    bool hasWirelessNode() const;
    void runAction(Action action);              // 提交动作本体
    void finishAction(Action action, bool ok, const QString &out);
    int  scanSeconds() const;                   // 读配置（wifi/scanSeconds）
    QString wpaCtrlDir() const;
    void submitScanStep(int step);
    bool scanOutputUsable(const QString &out) const;

    int  m_scanJob;
    int  m_statusJob;
    int  m_actionJob;
    int  m_action;              // Action
    WifiMode m_mode;
    int  m_scanStep;            // 0=wpa_cli scan 1=scan_results 2=iw
    int  m_scanSeq;             // 扫描轮次；旧定时器回调据此作废
    int  m_scanLeft;            // 倒计时剩余秒数
    QTimer *m_scanTimer;
    bool m_staInited;
    // 因为「还没切 STA」而拒绝了扫描时置位：等 ActEnterSta 成功后再自动扫一次，
    // 用户点一次扫描就该出结果，不该要求他点两次。
    bool m_pendingScanAfterSta;
    QString m_wlan;
    QString m_apSsid;
    QString m_apPassword;
    QString m_apSecurity;
    QString m_connectSsid;
    QString m_connectPassword;
    QString m_appliedLogLevel;     // 已下发过的等级，避免重复执行 wifi -D
    QStringList m_apClientMacs;    // 最近一次 wifi -l ap 解析出的设备 MAC
    QString     m_apIp;            // 热点自身 IP（wifi -l ap 输出里带的）
};

#endif // WIFISERVICE_H
