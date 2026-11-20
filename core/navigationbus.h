#ifndef HMI_CORE_NAVIGATIONBUS_H
#define HMI_CORE_NAVIGATIONBUS_H

#include <QObject>
#include <QString>

// 设计模式 #9 的第二个落点：观察者（Observer）+ 中介者（Mediator）——切页请求总线
//
// 为什么需要它：
//   GPADC 的 K1/K2/K3 要能绑到「上一页 / 下一页 / 回总览」这类动作，但
//     · services/ 不能认识 MainWindow（依赖方向只向下，见 docs/DESIGN-PATTERNS.md）；
//     · MainWindow 也不认识任何具体页面或服务（mainwindow.h 的类注释写死了这点）。
//   直接把 setCurrentPage 塞给服务层，等于把 UI 类型灌进 services/，
//   以后换掉 QStackedWidget 或加上网页端控制都得返工。
//
// 做法：把「请求切页」抽成一条总线。谁想切页就 request*()，MainWindow 订阅后执行。
// 将来新增切页来源（网页、串口命令、定时轮播）都不必改 MainWindow 的切页逻辑。
//
// 与 StatusBus 的区别：StatusBus 传的是「设备现在什么状态」（可缓存、需去重），
// 这里是「请切到某页」（一次性命令、不需要快照），所以不保存历史、只保留当前页 id。
class NavigationBus : public QObject
{
    Q_OBJECT
public:
    static NavigationBus *instance();

    // 按页面 id 切页。id 与 HMI_REGISTER_PAGE 的第一个参数一致（"wifi" / "bt" / ...），
    // 而不是 QStackedWidget 的下标——下标会因为新增页面的 order 变化而错位。
    void requestPage(const QString &pageId);

    // 相对当前页翻页：delta=+1 下一页、-1 上一页，越界自动绕回。
    // 绕回而不是夹住：从最后一页再按「下一页」回到总览更符合按键的使用直觉。
    void requestOffset(int delta);

    void requestHome();

    // 当前页 id，由 MainWindow 在每次切页后回报。未切过页时为空。
    QString currentPageId() const;
    void setCurrentPageId(const QString &pageId);

signals:
    void pageRequested(const QString &pageId);
    void offsetRequested(int delta);
    void homeRequested();

private:
    explicit NavigationBus(QObject *parent = nullptr);

    QString m_currentPageId;
};

#endif // HMI_CORE_NAVIGATIONBUS_H
