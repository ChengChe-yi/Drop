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
        return PickupSuppress::Init();
    }

    void Uninit()
    {
        PickupSuppress::Uninit();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}