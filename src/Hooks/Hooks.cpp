#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include "PickupSuppress.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing hooks ...");

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
