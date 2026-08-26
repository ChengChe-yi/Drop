#include "pch.h"
#include "Hooks.h"
#include "PickupSuppress.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing hooks (FF 25 JMP)...");

        // Only the pickup hook is active right now; PillarSuppress is
        // deliberately disabled (we just don't call its Init here).
        bool okPickup = PickupSuppress::Init();

        LOG("Hooks", "pickup=%d", (int)okPickup);
        return okPickup;
    }

    void Uninit()
    {
        PickupSuppress::Uninit();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}
