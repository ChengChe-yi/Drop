#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include "PickupSuppress.h"
#include "InteeProbe.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing hooks ...");
        // 探测 hook 是附加诊断，安装结果由 InteeProbe 自己的日志呈现；
        // 只要屏蔽 hook 在位就算插件加载成功。
        bool suppress = PickupSuppress::Init();
        InteeProbe::Init();
        return suppress;
    }

    void Uninit()
    {
        InteeProbe::Uninit();
        PickupSuppress::Uninit();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}