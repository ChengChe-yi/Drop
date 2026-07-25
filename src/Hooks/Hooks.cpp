#include "pch.h"
#include "Hooks.h"
#include "SetActiveHook.h"
#include "PickupSuppress.h"
#include "PillarSuppress.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing hooks (FF 25 JMP)...");

        bool sa = SetActiveHook::Init();

        LOG("Hooks", "SAdiag=%d", (int)sa);
        return sa;
    }

    void Uninit()
    {
        SetActiveHook::Cancel();
        SetActiveHook::Uninit();
        PickupSuppress::Uninit();
        PillarSuppress::Uninit();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}
