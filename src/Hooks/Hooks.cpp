#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include <MinHook.h>
#include "PickupSuppress.h"
#include "InteeProbe.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing hooks ...");
        bool suppress = PickupSuppress::Init();
        InteeProbe::Init();
        return suppress;
    }

    void Uninit()
    {
        InteeProbe::Uninit();
        PickupSuppress::Uninit();
        MH_Uninitialize();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}