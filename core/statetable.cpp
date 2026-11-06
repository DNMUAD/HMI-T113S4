#include "statetable.h"

namespace {

// 转移白名单：rows = 当前状态，cols = 目标状态
// 顺序：Unknown, Off, Busy, Ready, Error
const bool kAllowed[5][5] = {
    // → Unknown  Off    Busy   Ready  Error
    {   true,    true,  true,  true,  true },   // Unknown
    {   true,    false, true,  true,  true },   // Off
    {   true,    true,  false, true,  true },   // Busy
    {   true,    true,  true,  false, true },   // Ready
    {   true,    true,  true,  true,  false }   // Error
};

} // namespace

StateTransitionTable::StateTransitionTable()
    : m_current(DeviceUnknown)
{
    for (int from = 0; from < 5; ++from)
        for (int to = 0; to < 5; ++to)
            m_allowed[from][to] = kAllowed[from][to];
}

StateTransitionTable::StateTransitionTable(DeviceState initial)
    : m_current(initial)
{
    for (int from = 0; from < 5; ++from)
        for (int to = 0; to < 5; ++to)
            m_allowed[from][to] = kAllowed[from][to];
}

DeviceState StateTransitionTable::current() const
{
    return m_current;
}

bool StateTransitionTable::accepts(DeviceState next) const
{
    if (next == m_current)
        return true;                       // 幂等
    return m_allowed[int(m_current)][int(next)];
}

bool StateTransitionTable::transit(DeviceState next)
{
    if (!accepts(next))
        return false;
    m_current = next;
    return true;
}

void StateTransitionTable::reset(DeviceState state)
{
    m_current = state;
}
