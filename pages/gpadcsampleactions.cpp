#include "gpadcsampleactions.h"
#include "gpadcservice.h"
#include "navigationbus.h"

#include <QDebug>
#include <QObject>
#include <QString>

#include <functional>

namespace {

// 动作 id 统一前缀 gpadc.：将来别处也可能注册 nav-* 之类的 id，
// 前缀能避免撞名，也能让人一眼看出这些动作是「按键侧的示例」。
// addAction() 同 id 视为覆盖，所以本函数重复调用是安全的（幂等）。
void addNavAction(const QString &id, const QString &name, const QString &description,
                  const std::function<void ()> &fn)
{
    GpadcService::instance()->addAction(
        id, name, description,
        [fn, id](const QString &keyName, int code) {
            // 日志里带上「哪个键、哪个动作 id 触发的」，排查绑定错乱时有用。
            // 记 Debug：每次按键都会走这里，Info 会把日志面板刷满。
            Q_UNUSED(code)
            qDebug("GPADC %s 触发动作 %s", qPrintable(keyName), qPrintable(id));
            fn();
        });
}

} // namespace

void registerGpadcSampleActions()
{
    GpadcService *svc = GpadcService::instance();
    if (!svc)
        return;

    // ---- 相对切页：最常用的两个，建议绑在 K1 / K2 ----
    addNavAction(QStringLiteral("gpadc.nav-prev"), QObject::tr("上一页"),
                 QObject::tr("切到导航里的上一页，总览处绕回最后一页"),
                 []() { NavigationBus::instance()->requestOffset(-1); });

    addNavAction(QStringLiteral("gpadc.nav-next"), QObject::tr("下一页"),
                 QObject::tr("切到导航里的下一页，最后一页绕回总览"),
                 []() { NavigationBus::instance()->requestOffset(1); });

    addNavAction(QStringLiteral("gpadc.nav-home"), QObject::tr("回到总览"),
                 QObject::tr("直接切回总览页"),
                 []() { NavigationBus::instance()->requestHome(); });

    // ---- 绝对跳转：演示「按 id 切页」，比相对切页更好用的是直达某页 ----
    //
    // 这里只放两个示例（WiFi / 蓝牙），不把 12 个页面全铺进下拉框：
    // 下拉框是要给用户看的，塞 12 条导航等于把页面列表抄一遍，
    // 反而把「音量＋」「播放」这类真正常用的动作挤到后面。
    // 要加别的页，照这两条复制，改 id 与文案即可。
    addNavAction(QStringLiteral("gpadc.goto-wifi"), QObject::tr("跳到 WiFi 页"),
                 QObject::tr("按页面 id 直达 pages/wifipage"),
                 []() { NavigationBus::instance()->requestPage(QStringLiteral("wifi")); });

    addNavAction(QStringLiteral("gpadc.goto-bt"), QObject::tr("跳到蓝牙页"),
                 QObject::tr("按页面 id 直达 pages/btpage"),
                 []() { NavigationBus::instance()->requestPage(QStringLiteral("bt")); });

    // 注册完把「动作表变了」这件事说一声：调试时能确认注册点确实跑到了。
    // 用 Debug 级别：正常情况下用户不需要看见这一行。
    qDebug("GPADC 示例动作已注册，动作表共 %d 项", int(svc->actions().size()));
}
