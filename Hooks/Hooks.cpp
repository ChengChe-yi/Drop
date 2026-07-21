#include "pch.h"
#include "Hooks.h"
#include "PillarSuppress.h"
#include "PickupSuppress.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing manual hooks...");

        bool pillar = PillarSuppress::Init();
        bool pickup = PickupSuppress::Init();

        LOG("Hooks", "PillarSuppress=%d PickupSuppress=%d", (int)pillar, (int)pickup);

        if (!pillar && !pickup)
        {
            LOG_MSG("Hooks", "All hooks FAILED!");
            return false;
        }

        LOG_MSG("Hooks", "Hooks installed");
        return true;
    }

    void Uninit()
    {
        PillarSuppress::Uninit();
        PickupSuppress::Uninit();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}
