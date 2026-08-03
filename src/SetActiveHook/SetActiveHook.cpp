#include "pch.h"
#include "SetActiveHook.h"
#include "Logger.h"

namespace SetActiveHook
{
    // NOTE: The LoginMainPage trampoline hook has been disabled.
    // Pickup / pillar hooks are now installed directly by Hooks::Init().
    // This entry point is kept as a no-op so existing call sites and
    // symbol references keep working.
    bool Init()
    {
        LOG_MSG("SAdiag", "SetActiveHook::Init is a no-op (hook disabled)");
        return true;
    }

    void Uninit()
    {
        LOG_MSG("SAdiag", "SetActiveHook::Uninit (no-op)");
    }

    void Cancel()
    {
        LOG_MSG("SAdiag", "SetActiveHook::Cancel (no-op)");
    }
}
