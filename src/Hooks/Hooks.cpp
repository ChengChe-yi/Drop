#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include <MinHook.h>
#include "InteeBtn.h"
#include "PickupFilter.h"
#include "InteeProbe.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing hooks ...");
        InteeBtn::Init();
        bool suppress = PickupFilter::Init();
        InteeProbe::Init();
        return suppress;
    }

    void Uninit()
    {
        InteeProbe::Uninit();
        PickupFilter::Uninit();
        MH_Uninitialize();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}