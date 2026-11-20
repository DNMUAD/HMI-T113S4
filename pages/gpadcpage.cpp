#include "gpadcpage.h"
#include "cardbutton.h"
#include "gpadcsampleactions.h"
#include "gpadcservice.h"
#include "pagefactory.h"
#include "uimetrics.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>

GpadcPage::GpadcPage(QWidget *parent)
    : BasePage(tr("GPADC 按键"), parent)
    , m_device(nullptr)
    , m_last(nullptr)
{
    // 在构造函数里注册示例动作（切页三件套 + 两个直达页）。
    //
    // 为什么放这里而不是 GpadcService 的构造函数：
    // 动作的实现要用 NavigationBus 的「切页」语义，那是 UI 概念；
    // services/ 不该替页面决定有哪些 UI 动作。注册点在使用方，依赖方向才向下。
    //
    // 为什么放构造函数而不是 buildContent()：
    // buildContent() 由 PageFactory 建页时调用，时机上等价；
    // 但注册是「本页带着哪些动作」的固有属性，放构造函数更贴近语义。
    // 重复调用安全：addAction() 同 id 视为覆盖。
    registerGpadcSampleActions();
}

void GpadcPage::buildContent()
{
    // 提示必须压到一行，宽度按 800px 屏减去导航/边距后约 600px 估算，
    // 中文 15px 一行约 34 字。超了就换行，两行会把下面的按钮顶出可视区
    // （WiFi 页早期踩过同一个坑）。
    setHint(tr("键码 115(0x73)/119(0x77)/373(0x175)；下拉框已内置「上一页/下一页」等示例。"));

    m_device = new QLabel(tr("设备：未查找"), this);
    m_device->setObjectName(QStringLiteral("pageStatus"));
    addWidget(m_device);

    m_last = new QLabel(tr("最近按键：-"), this);
    m_last->setObjectName(QStringLiteral("pageStatus"));
    addWidget(m_last);

    // ---- 三个键各一行：说明 + 按下次数 + 动作下拉框 ----
    //
    // 用「一行一个 QHBoxLayout」而不是 addGrid()：下拉框要占满右半边，
    // 网格两列等宽的约束会把它压得只剩一半宽度，长动作名直接显示不全。
    const QList<GpadcService::KeyInfo> keys = GpadcService::keys();
    for (const GpadcService::KeyInfo &key : keys) {
        QWidget *holder = new QWidget(this);
        QHBoxLayout *row = new QHBoxLayout(holder);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(UiMetrics::PageSpacing);

        QLabel *caption = new QLabel(QStringLiteral("%1  %2").arg(key.name, key.note), holder);
        caption->setObjectName(QStringLiteral("keyCaption"));
        caption->setMinimumWidth(150);

        QLabel *count = new QLabel(tr("0 次"), holder);
        count->setObjectName(QStringLiteral("keyCount"));
        count->setMinimumWidth(56);
        count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QComboBox *action = new QComboBox(holder);
        action->setObjectName(QStringLiteral("keyActionBox"));
        action->setMinimumWidth(180);

        row->addWidget(caption);
        row->addWidget(count);
        row->addWidget(action, 1);
        addWidget(holder);

        Row r;
        r.code = key.code;
        r.count = count;
        r.action = action;
        m_rows << r;

        // 用户改下拉框 → 立即写入绑定并落盘（GpadcService::setBinding 负责持久化）
        //
        // 捕获 code 用「变量 + 拷贝」而不是 C++14 的初始化捕获（[code = key.code]）：
        // 本工程 CONFIG += c++11，初始化捕获是 C++14 语法，交叉编译器会直接报错。
        const int code = key.code;
        connect(action, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, code, action](int index) {
            if (index < 0)
                return;
            const QString id = action->itemData(index).toString();
            GpadcService::instance()->setBinding(code, id);
            setStatus(tr("绑定已更新：%1").arg(GpadcService::instance()->bindingSummary()));
        });
    }

    // ---- 四个按钮排一行（单行样式 42px）----
    //
    // 不套 addGrid() 的两列两行：那样要多占 58px 的第二行高度，
    // 一屏放不下。四个按钮各占 1/4 宽，触摸目标仍有约 170x42。
    // 说明文字从按钮副标题改成 tooltip：空间不够时信息不该消失，只是不该占位。
    QWidget *buttonRow = new QWidget(this);
    QHBoxLayout *buttons = new QHBoxLayout(buttonRow);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(UiMetrics::PageSpacing);

    CardButton *findBtn = newButton(tr("查找设备"), [this]() { refreshDevice(); },
                                    QString(), true);
    CardButton *startBtn = newButton(tr("开始监听"), [this]() {
        if (GpadcService::instance()->start()) {
            setCounting(true);
        } else {
            setStatus(tr("启动监听失败，见下方日志"));
        }
        refreshDevice();
    }, QString());
    CardButton *stopBtn = newButton(tr("停止监听"), [this]() {
        GpadcService::instance()->stop();
        setCounting(false);
        refreshDevice();
    }, QString());
    CardButton *clearBtn = newButton(tr("计数清零"), [this]() {
        GpadcService::instance()->resetCounters();
        refreshCounters();
        setStatus(tr("按键计数已清零"));
    }, QString());

    findBtn->setToolTip(tr("扫 /proc/bus/input/devices，按名字含 gpadc 匹配"));
    startBtn->setToolTip(tr("打开 eventN，用 QSocketNotifier 异步读事件"));
    stopBtn->setToolTip(tr("关闭 fd 并释放 eventN"));
    clearBtn->setToolTip(tr("只清显示计数，不改绑定"));

    buttons->addWidget(findBtn, 1);
    buttons->addWidget(startBtn, 1);
    buttons->addWidget(stopBtn, 1);
    buttons->addWidget(clearBtn, 1);
    addWidget(buttonRow);

    // ---- 信号：按键回显 + 动作回显 + 设备节点变化 ----
    GpadcService *svc = GpadcService::instance();

    connect(svc, &GpadcService::keyPressed, this, [this, svc](const QString &name, int code) {
        m_last->setText(tr("最近按键：%1 按下（%2）· 动作「%3」")
                        .arg(name, GpadcService::codeText(code),
                             svc->actionName(svc->binding(code))));
        refreshCounters();
    });
    connect(svc, &GpadcService::keyReleased, this, [this](const QString &name, int) {
        m_last->setText(tr("最近按键：%1 松开").arg(name));
    });
    connect(svc, &GpadcService::actionTriggered, this,
            [this, svc](const QString &name, int, const QString &actionId) {
        // 动作被触发时单独记一行，方便确认「绑定是不是真生效了」。
        // 回显/无动作本身不产生业务，这里只在状态行提示，不刷日志。
        setStatus(tr("%1 → %2").arg(name, svc->actionName(actionId)));
    });
    // 节点变化统一走 refreshDevice()：它会带上「监听中 / 点开始监听」的后缀。
    // 直接在这里写 setText(node) 会在 refresh() 之后把后缀抹掉，
    // 表现为「监听明明在跑，界面却说未监听」。
    connect(svc, &GpadcService::deviceChanged, this, [this](const QString &) {
        refreshDevice();
    });

    rebuildActions();
    refreshDevice();
    refreshCounters();
    setStatus(tr("就绪 · 绑定 %1").arg(svc->bindingSummary()));
}

// 动作表可能在页面构造之后才被别的模块扩充（例如音量模块启动较晚），
// 所以每次进入页面都重填一次；重填期间用 QSignalBlocker 挡掉 currentIndexChanged，
// 否则每填一项都会被当成「用户改了绑定」。
void GpadcPage::rebuildActions()
{
    GpadcService *svc = GpadcService::instance();
    const QList<GpadcService::Action> actions = svc->actions();

    for (const Row &r : m_rows) {
        const QSignalBlocker blocker(r.action);
        r.action->clear();
        for (const GpadcService::Action &a : actions) {
            r.action->addItem(a.name, a.id);
            // 说明放 tooltip：板端没有鼠标，但长按/调试时仍能看到完整含义
            r.action->setItemData(r.action->count() - 1, a.description, Qt::ToolTipRole);
        }
        const int index = r.action->findData(svc->binding(r.code));
        // findData 找不到返回 -1；此时插一项把当前绑定如实显示出来，不静默落到第一项
        if (index >= 0) {
            r.action->setCurrentIndex(index);
        } else {
            r.action->addItem(svc->binding(r.code), svc->binding(r.code));
            r.action->setCurrentIndex(r.action->count() - 1);
        }
    }
}

void GpadcPage::refreshDevice()
{
    GpadcService *svc = GpadcService::instance();
    const QString node = svc->findEventNode();

    if (!node.isEmpty()) {
        m_device->setText(svc->isRunning()
                          ? tr("设备：%1（监听中）").arg(node)
                          : tr("设备：%1（点「开始监听」启用）").arg(node));
        return;
    }

    // 没找到时把实际看到的 input 设备列出来：板端排查最常见的情况就是
    // 「内核没开 GPADC」或「board.dts 里 &gpadc status 不是 okay」。
    const QStringList devices = svc->describeDevices();
    if (devices.isEmpty()) {
        // 空列表有两种成因，措辞要能同时覆盖，不能让用户以为一定是权限问题：
        //   · /proc/bus/input/devices 打不开（日志里会有一条 FAIL）；
        //   · 打得开但里面没有任何输入设备（桌面 Kit 的 /proc 常是这样）。
        m_device->setText(tr("设备：未找到 gpadc；/proc/bus/input/devices 中没有任何输入设备"
                             "（详见下方日志）"));
        return;
    }
    m_device->setText(tr("设备：未找到 gpadc。当前输入设备：%1")
                      .arg(devices.join(QStringLiteral("；"))));
}

void GpadcPage::refreshCounters()
{
    GpadcService *svc = GpadcService::instance();
    for (const Row &r : m_rows)
        r.count->setText(tr("%1 次").arg(svc->pressCount(r.code)));
}

void GpadcPage::setCounting(bool on)
{
    setStatus(on ? tr("监听中：%1").arg(GpadcService::instance()->eventNode())
                 : tr("未监听"));
}

void GpadcPage::onEnter()
{
    GpadcService *svc = GpadcService::instance();

    // 进页面即开始监听：按键是「随时想按就按」的输入，让用户先点一次
    // 「开始监听」才有反应，是没必要的仪式感。
    //
    // 只在查到真实 eventN 时才自动开：桌面 Kit 没有这个设备，
    // 无条件 start() 会在每次进页面时刷一条「仅支持目标板 Linux」的告警。
    if (!svc->isRunning() && !svc->findEventNode().isEmpty())
        svc->start();

    rebuildActions();          // 别的模块可能刚注册了新动作
    refreshDevice();
    refreshCounters();

    if (!svc->isRunning()) {
        setStatus(tr("就绪 · 绑定 %1").arg(svc->bindingSummary()));
        return;
    }
    setCounting(true);
}

HMI_REGISTER_PAGE("gpadc", 70, QT_TR_NOOP("GPADC"), GpadcPage)
