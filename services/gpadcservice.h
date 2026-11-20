#ifndef GPADCSERVICE_H
#define GPADCSERVICE_H

#include "deviceservice.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QSocketNotifier;

// GPADC 按键服务（板端 T113-S4）
//
// 内核侧已就绪，应用侧不动内核：
//   · CONFIG_SUNXI_GPADC=y（drivers/input/sensor/sunxi_gpadc.c）；
//   · board.dts 的 &gpadc 用 key_cnt / keyN_vol / keyN_val 把「电压区间」翻译成
//     键码，再经 input 子系统上报到 /dev/input/eventN；
//   · 上报类型 EV_KEY：value=1 按下、0 松开、2 自动重复（驱动按 EV_REP 注册时）。
//
// 两个实测踩过的坑：
//   1. input 设备名是 "sunxi-gpadc0"，**不是** "gpadc"。照文档按
//      Name="gpadc" 精确匹配永远找不到节点 —— 本类按「名字含 gpadc」匹配，
//      大小写不敏感，且只在 H: Handlers= 行里取 eventN。
//   2. 键码是十进制 115/119/373，而 hexdump 里看到的是 4 位十六进制
//      0073/0077/0175。别把 hexdump 的 0073 当成十进制的 73。
//
// 职责边界：发现设备 → 读事件 → 翻译成 K1/K2/K3 → 派发「当前绑定的动作」。
// 按键**不写死**响应动作：绑定关系由 setBinding() 决定，页面用下拉框选；
// 新动作通过 addAction() 注册进来，本文件不需要认识它们。
class GpadcService : public DeviceService
{
    Q_OBJECT
public:
    // 板端实测键码（与 board.dts 的 key0_val / key2_val / key3_val 一致）。
    // 括注是内核 input-event-codes.h 的定义，只作参考，不代表业务含义；
    // 本项目不按 KEY_* 语义派发，一律走「绑定表 → 动作」。
    enum KeyCode
    {
        KeyK1 = 115,        // hexdump 0073 = 0x73  = 115，KEY_VOLUMEUP
        KeyK2 = 119,        // hexdump 0077 = 0x77  = 119，KEY_PAUSE
        KeyK3 = 373         // hexdump 0175 = 0x175 = 373，KEY_MODE
    };

    // 一个物理按键：键码 + 显示名 + 说明（说明只用于界面提示）
    struct KeyInfo
    {
        int     code;
        QString name;
        QString note;
    };

    // 一个可绑定动作：id 是稳定标识，name 是下拉框文案
    struct Action
    {
        QString id;
        QString name;
        QString description;
    };

    // 动作处理函数：参数是按键显示名与键码，返回值无意义
    typedef std::function<void (const QString &keyName, int code)> ActionHandler;

    static GpadcService *instance();

    static QList<KeyInfo> keys();                   // K1 / K2 / K3
    static QString keyName(int code);               // "K1"；未登记返回 "键 0x…"
    static QString codeText(int code);              // "0x73 (115)"

    // ---- 动作表（注册表模式，页面下拉框的数据源）----
    // 默认只注册两个内置动作：
    //   "none" 无动作    —— 只记 Debug 日志，不触发任何业务
    //   "echo" 仅回显    —— 进日志 + 页面计数，不触发任何业务
    // 其他模块可以这样把自己的动作挂进来，GPADC 不用改：
    //     GpadcService::instance()->addAction(
    //         QStringLiteral("volume-up"), tr("音量 +"), tr("每按一次 +5"),
    //         [](const QString &, int) { MediaService::instance()->adjustVolume(5); });
    QList<Action> actions() const;
    void addAction(const QString &id, const QString &name, const QString &description,
                   ActionHandler handler = ActionHandler());
    QString actionName(const QString &id) const;    // id → 显示名，未知则原样返回

    // ---- 绑定：键码 → 动作 id ----
    QString binding(int code) const;                // 未设置时返回 "echo"
    void setBinding(int code, const QString &actionId);
    void resetBindings();                           // 全部回到 "echo"
    QString bindingSummary() const;                 // "K1→仅回显 / K2→仅回显 / K3→仅回显"

    // ---- 按下计数（页面回显用，服务侧单一来源）----
    int  pressCount(int code) const;
    void resetCounters();

    // ---- 设备 ----
    QString findEventNode() const;                  // /dev/input/eventN，找不到返回空
    QStringList describeDevices() const;            // 全部 input 设备："名字 → 节点"
    QString eventNode() const;

    bool start();
    void stop();
    bool isRunning() const;

    // 注入一个按键事件，走与真实硬件完全相同的处理路径
    // （handleCode → keyPressed → dispatch → 动作处理器）。
    //
    // 用途：
    //   · 桌面 Kit 没有 /dev/input/eventN，靠它验证界面与绑定；
    //   · --gpadc-selftest 无人值守跑通整条链路。
    // 真实硬件上不会调用它，但两条路径共用同一段代码，
    // 所以「测试通过」确实意味着硬件路径的派发逻辑正确。
    void simulateKey(int code, int value);

    void refresh() override;                        // 供「刷新状态栏」「环境自检」调用

signals:
    void keyEvent(int code, const QString &name, int value);          // 1 按下 / 0 松开 / 2 重复
    void keyPressed(const QString &name, int code);
    void keyReleased(const QString &name, int code);
    void actionTriggered(const QString &name, int code, const QString &actionId);
    void deviceChanged(const QString &eventNode);

private:
    explicit GpadcService(QObject *parent = nullptr);

    void onActivated();
    void handleCode(int code, int value);
    void dispatch(int code);

    int                           m_fd;
    QSocketNotifier              *m_notifier;
    QString                       m_eventNode;
    QList<Action>                 m_actions;
    QHash<QString, ActionHandler> m_handlers;
    QHash<int, QString>           m_bindings;
    QHash<int, int>               m_pressCount;
};

#endif // GPADCSERVICE_H
