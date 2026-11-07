#ifndef HMI_CORE_PAGEFACTORY_H
#define HMI_CORE_PAGEFACTORY_H

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class BasePage;
class QWidget;

// 设计模式 #11：工厂方法（Factory）+ 自注册表（Registry）
//
// 页面不再由 MainWindow 硬编码 new 出来，而是各自在文件末尾登记一条
// 「id + 导航文案 + 顺序 + 构造器」。MainWindow 只遍历注册表建页、建导航，
// 于是「加一个页面」= 新建文件 + 一行注册，符合开闭原则；
// 导航文案在 entries() 时才 translate()，避免静态初始化期翻译未装载。
class PageFactory
{
public:
    typedef std::function<BasePage *(QWidget *)> Creator;

    struct Entry
    {
        QString id;
        QString navText;      // 已翻译；空表示不占导航项（总览页）
        int     order;
        Creator creator;
    };

    // 登记项：pagefactory.cpp 的排序比较器要拿它当参数类型，
    // 因此必须放在 public 段（嵌套类型同样受访问控制）。
    struct Registration
    {
        QString     id;
        const char *navSource;
        int         order;
        Creator     creator;
    };

    static PageFactory *instance();

    // navSource 传未翻译的源串（QT_TR_NOOP 的字符串字面量），nullptr 表示不进导航
    void addPage(const QString &id, const char *navSource, int order, Creator creator);

    BasePage *create(const QString &id, QWidget *parent = nullptr) const;   // 建页并 initialize()
    QList<BasePage *> createAll(QWidget *parent = nullptr) const;
    QList<Entry> entries() const;                       // 按 order 升序
    QStringList ids() const;

private:
    PageFactory();

    QList<Registration> m_registrations;
};

// 页面实现文件末尾调用一次：
//   HMI_REGISTER_PAGE("wifi", 10, QT_TR_NOOP("WiFi"), WifiPage)
#define HMI_REGISTER_PAGE(PAGE_ID, PAGE_ORDER, NAV_SOURCE, PAGE_CLASS)                       \
    namespace {                                                                              \
    struct PAGE_CLASS##PageRegistrar                                                         \
    {                                                                                        \
        PAGE_CLASS##PageRegistrar()                                                          \
        {                                                                                    \
            PageFactory::instance()->addPage(                                                \
                QStringLiteral(PAGE_ID), NAV_SOURCE, PAGE_ORDER,                             \
                [](QWidget *parent) -> BasePage * { return new PAGE_CLASS(parent); });       \
        }                                                                                    \
    };                                                                                       \
    static PAGE_CLASS##PageRegistrar g_pageRegistrar_##PAGE_CLASS;                           \
    }

#endif // HMI_CORE_PAGEFACTORY_H
