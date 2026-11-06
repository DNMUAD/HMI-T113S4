#ifndef HMI_CORE_STATETABLE_H
#define HMI_CORE_STATETABLE_H

#include "devicestatus.h"

// 设计模式 #7：状态（State，表驱动变体）
//
// 每个设备一份状态转移表：只接受白名单内的迁移。
//  · 「就绪 → 就绪」的重复上报被吞掉（detail 变了除外，交给 StatusBus 判断）；
//  · 非法跳变（比如 未探测 → 处理中 直接跳到 就绪）会被拦下并记日志。
//
// 相比「每个状态一个多态类」的经典 State，这里状态很少且转移规则是数据，
// 用表驱动更省代码、也更方便在 ini 里配置；迁移到多态 State 时接口不变。
class StateTransitionTable
{
public:
    StateTransitionTable();
    explicit StateTransitionTable(DeviceState initial);

    DeviceState current() const;

    // 是否允许迁移到 next（同状态视为幂等，返回 true）
    bool accepts(DeviceState next) const;

    // 允许则迁移并返回 true
    bool transit(DeviceState next);

    void reset(DeviceState state);

private:
    DeviceState m_current;
    bool        m_allowed[5][5];
};

#endif // HMI_CORE_STATETABLE_H
