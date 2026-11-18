#include "ibtbackend.h"

#include "fakebtbackend.h"

#ifdef HMI_HAVE_BT
#include "btmgbackend.h"
#endif

// 工厂：把「编进哪个后端」这件事收在一处。
// 上层（BtService）只认 IBtBackend，换实现只改这里一行。
IBtBackend *createBtBackend(bool simulated)
{
#ifdef HMI_HAVE_BT
    if (!simulated)
        return new BtmgBackend;
#else
    Q_UNUSED(simulated)
#endif
    // 桌面 Kit（没有 HMI_HAVE_BT）或显式 --simulate 都落到假实现
    return new FakeBtBackend;
}

bool btBackendCompiledIn()
{
#ifdef HMI_HAVE_BT
    return true;
#else
    return false;
#endif
}
