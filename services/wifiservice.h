#ifndef WIFISERVICE_H
#define WIFISERVICE_H

#include "deviceservice.h"

#include <QString>

class CommandSequence;

// AIC8800D80。命令全部经 ProcessRunner，参数化传参，不拼 shell。
// 状态经 StatusBus 广播（状态栏 / WiFi 页都从总线取，不再各自 connect 服务）。
//
// ── 链路分工（这是本类的核心约定）────────────────────────────────
//
// 开机阶段由 /etc/init.d/S41wifi 完成（S41：排在 S40network 之后、
// S70launcher 之前，所以界面起来时链路必然已经就绪）：
//
//   1. insmod aic8800_bsp.ko + aic8800_fdrv.ko   —— 挂驱动，出 wlan0
//   2. ifconfig wlan0 up                          —— 打开节点
//   3. wifi_daemon                                —— WiFi 管理服务（常驻）
//
// 本服务只负责开机之后的部分：
//
//   4. wifi -o sta                                —— 切 Station 模式
//   5. wpa_cli -p <ctrl> -i wlan0 scan / scan_results —— 扫描
//      （wifi -s 只把命令写进 socket 就退出，结果打在 daemon 的 stdout，
//        程序抓不到，所以扫描改走 supplicant 的控制接口）
//   6. wifi -c <ssid> <passwd>                    —— 连接（内部调 udhcpc 取 IP）
//
// 所以这里不再自己 insmod / 起 daemon。原因有三：
//   ① 那套逻辑已收敛到 S41wifi 一个入口，应用内再写一份就是两份实现，迟早不一致；
//   ② 会和开机脚本抢设备——重复 insmod 只会拿到 EEXIST，重复起 daemon 会抢
//      /tmp/UNIX_WIFI.domain 这个 socket；
//   ③ daemon 是常驻进程，用 ProcessRunner 起它要走 3–5s 超时钳制，很容易被误杀。
//
// 脚本是幂等的（模块已装载则跳过、daemon 在跑则跳过），所以链路没起来时
// 直接重跑 `S41wifi start` 就是最省事也最一致的修复手段，不需要第二份 C++ 实现。
class WifiService : public DeviceService
{
    Q_OBJECT
public:
    static WifiService *instance();

    bool driverReady() const;      // wlan0 是否已存在（开机脚本是否已装载驱动）
    bool daemonReady() const;      // wifi_daemon 是否已在运行
    bool linkReady() const;        // 两者都就绪 = 开机脚本已经把链路拉起来了

    void prepare();                // 确保链路就绪（必要时重跑 S41wifi start）→ 切 STA
    void restartLink();            // stop + start 修复链路（daemon 卡死时用）
    void initSta();                // 第 4 步：wifi -o sta
    void initAp();                 // wifi -o ap（进阶）
    void scan();                   // 扫描：wpa_cli（-p 指定 socket 目录），iw 兜底
    void connectTo(const QString &ssid, const QString &password);  // 第 6 步
    void disconnect();             // wifi -d
    void refresh() override;       // ip -4 addr show wlan0

    // 命令模式：把「确认链路 → STA → 连接 → 取 IP」封装成可排队的命令序列
    CommandSequence *makeConnectSequence(const QString &ssid, const QString &password);

signals:
    void scanResult(const QString &raw);
    void prepared(bool ok);        // 链路（wlan0 + wifi_daemon）就绪

private slots:
    void onFinished(int jobId, int exitCode, int exitStatus, const QString &out);
    void onFailed(int jobId, const QString &reason);

private:
    explicit WifiService(QObject *parent = nullptr);

    // prepare() 成功后要接着做的事。
    // 开机脚本只到 wifi_daemon 为止，而「扫描」和「连接」都必须在 STA 模式下
    // 才能执行，所以两者都先走 prepare 补齐 STA，再落地各自的动作。
    enum PendingAction {
        PendingNone = 0,
        PendingConnect,
        PendingScan
    };

    void detectInterfaces();
    bool hasWirelessNode() const;
    void stepPrepareNext();
    void runPendingAction();                           // prepare 结束后执行挂起的动作
    void doConnect();                                  // 连接动作本体
    void doScan();                                     // 扫描动作本体
    void submitScanStep();                             // 提交当前扫描数据源
    bool scanOutputUsable(const QString &out) const;   // 输出里是否真有 BSS
    void failPrepare(DeviceState state, const QString &detail);  // 准备失败的统一收尾
    bool continueLinkFix(const QString &failReason);   // restartLink 的后两段（stop → start → prepare）

    int  m_scanJob;
    int  m_statusJob;
    int  m_actionJob;
    int  m_scanStep;               // 扫描步骤：0=wpa_cli scan 1=scan_results 2=iw
    int  m_scanSeq;                // 扫描轮次；旧定时器回调据此丢弃，防重复点击叠加
    bool m_preparing;
    bool m_restarting;             // restartLink 流程进行中
    int  m_prepareStep;
    int  m_fixStep;                // 1=stop 已提交，2=start 已提交
    bool m_staInited;              // 本进程内是否已成功切过 wifi -o sta
    int  m_pendingAction;          // PendingAction，prepare 完成后执行
    QString m_pendingSsid;
    QString m_pendingPassword;
    QString m_wlan;                // 实际无线网卡名（板端 wlan0，桌面可能没有）
};

#endif // WIFISERVICE_H
