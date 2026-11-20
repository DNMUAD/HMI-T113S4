#include "navigationbus.h"
#include "singleton.h"

NavigationBus *NavigationBus::instance()
{
    return hmi::singleton<NavigationBus>([]() { return new NavigationBus; });
}

NavigationBus::NavigationBus(QObject *parent)
    : QObject(parent)
{
}

void NavigationBus::requestPage(const QString &pageId)
{
    if (pageId.isEmpty())
        return;
    emit pageRequested(pageId);
}

void NavigationBus::requestOffset(int delta)
{
    if (delta == 0)
        return;                      // 0 偏移无意义，直接丢弃，别让订阅方白跑一趟
    emit offsetRequested(delta);
}

void NavigationBus::requestHome()
{
    emit homeRequested();
}

QString NavigationBus::currentPageId() const
{
    return m_currentPageId;
}

void NavigationBus::setCurrentPageId(const QString &pageId)
{
    m_currentPageId = pageId;
}
