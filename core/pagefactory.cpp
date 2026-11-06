#include "pagefactory.h"
#include "basepage.h"
#include "logpanel.h"
#include "singleton.h"

#include <QCoreApplication>

#include <algorithm>

namespace {

bool orderLess(const PageFactory::Registration &a, const PageFactory::Registration &b)
{
    return a.order < b.order;
}

} // namespace

PageFactory *PageFactory::instance()
{
    return hmi::singleton<PageFactory>([]() { return new PageFactory; });
}

PageFactory::PageFactory()
{
}

void PageFactory::addPage(const QString &id, const char *navSource, int order, Creator creator)
{
    if (id.isEmpty() || !creator)
        return;

    for (int i = 0; i < m_registrations.size(); ++i) {
        if (m_registrations.at(i).id == id) {
            LogPanel::post(QStringLiteral("页面 id 重复注册，已忽略：%1").arg(id), LogPanel::Warn);
            return;
        }
    }

    Registration reg;
    reg.id = id;
    reg.navSource = navSource;
    reg.order = order;
    reg.creator = creator;
    m_registrations.append(reg);
}

QList<PageFactory::Entry> PageFactory::entries() const
{
    QList<Registration> sorted = m_registrations;
    std::stable_sort(sorted.begin(), sorted.end(), orderLess);

    QList<Entry> result;
    for (int i = 0; i < sorted.size(); ++i) {
        const Registration &reg = sorted.at(i);
        Entry entry;
        entry.id = reg.id;
        entry.order = reg.order;
        entry.creator = reg.creator;
        entry.navText = reg.navSource
                        ? QCoreApplication::translate("PageFactory", reg.navSource)
                        : QString();
        result.append(entry);
    }
    return result;
}

BasePage *PageFactory::create(const QString &id, QWidget *parent) const
{
    for (int i = 0; i < m_registrations.size(); ++i) {
        const Registration &reg = m_registrations.at(i);
        if (reg.id != id)
            continue;
        BasePage *page = reg.creator(parent);
        if (page)
            page->initialize();     // 模板方法：骨架已就绪，这里跑子类内容
        return page;
    }
    LogPanel::post(QStringLiteral("未注册的页面 id：%1").arg(id), LogPanel::Error);
    return nullptr;
}

QList<BasePage *> PageFactory::createAll(QWidget *parent) const
{
    QList<BasePage *> pages;
    const QList<Entry> all = entries();
    for (int i = 0; i < all.size(); ++i) {
        BasePage *page = create(all.at(i).id, parent);
        if (page)
            pages.append(page);
    }
    return pages;
}

QStringList PageFactory::ids() const
{
    QStringList result;
    const QList<Entry> all = entries();
    for (int i = 0; i < all.size(); ++i)
        result << all.at(i).id;
    return result;
}
