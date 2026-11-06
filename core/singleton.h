#ifndef HMI_CORE_SINGLETON_H
#define HMI_CORE_SINGLETON_H

// 设计模式 #1：单例（Singleton，懒加载）
//
// 全工程的 __instance() 都走这一处模板，避免每个类各写一份
// 「static 指针 + 判空 + new」的复制粘贴代码。
// 用工厂函数注入构造，构造函数声明为 private 的类也能安全使用。
//
// 说明：嵌入式单线程 GUI（init.d 拉起，唯一主线程）下够用；
// 若要跨线程首访，把 factory 换成 std::call_once 即可，调用方无需改动。

namespace hmi {

template <typename T, typename Factory>
T *singleton(Factory factory)
{
    static T *instance = nullptr;
    if (!instance)
        instance = factory();
    return instance;
}

} // namespace hmi

#endif // HMI_CORE_SINGLETON_H
