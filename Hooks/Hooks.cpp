#include "pch.h"
#include "Hooks.h"
#include "PillarSuppress.h"
#include <MinHook.h>

#define LOG(fmt, ...) do {                          \
    char _b_[512];                                  \
    sprintf_s(_b_, "[Drop] " fmt "\n", __VA_ARGS__);\
    OutputDebugStringA(_b_);                        \
} while(0)

namespace Hooks
{
    bool Init()
    {
        if (MH_Initialize() != MH_OK) { /* may already be initialized */ }

        if (!PillarSuppress::Init())
        {
            LOG("PillarSuppress::Init FAILED");
            return false;
        }

        LOG("All hooks installed");
        return true;
    }

    void Uninit()
    {
        PillarSuppress::Uninit();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        LOG("All hooks uninstalled");
    }
}
