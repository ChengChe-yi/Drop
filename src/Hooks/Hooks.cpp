#include "pch.h"
#include "Hooks.h"
#include "PickupSuppress.h"
#include "PillarSuppress.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing hooks (FF 25 JMP)...");

        // SetActiveHook is currently disabled — see SetActiveHook::Init().
        // We register the pickup / pillar hooks directly here.
        bool okPickup = PickupSuppress::Init();
        bool okPillar = PillarSuppress::Init();

        LOG("Hooks", "pickup=%d pillar=%d", (int)okPickup, (int)okPillar);
        return okPickup && okPillar;
    }

    void Uninit()
    {
        PickupSuppress::Uninit();
        PillarSuppress::Uninit();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}
