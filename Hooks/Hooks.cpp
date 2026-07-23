#include "pch.h"
#include "Hooks.h"
#include "PillarSuppress.h"
#include "PickupSuppress.h"
#include "Stealth.h"
#include "Logger.h"

namespace Hooks
{
    bool Init()
    {
        LOG_MSG("Hooks", "Initializing INT3+VEH hooks...");

        if (!Int3Hook::InitVEH()) {
            LOG_MSG("Hooks", "InitVEH FAILED");
            return false;
        }

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
        Int3Hook::ShutdownVEH();
        LOG_MSG("Hooks", "All hooks uninstalled");
    }
}

// ============================================================================
// INT3 + VEH infrastructure
// ============================================================================

namespace {

    struct Int3Entry {
        uint8_t* target;
        uint8_t  origByte;
        void*    thunk;
    };

    Int3Entry g_hooks[4] = {};
    int g_hookCount = 0;
    void* g_vehHandle = nullptr;

    // Cached VirtualProtect pointer (resolved via Stealth to bypass IAT)
    BOOL (WINAPI* g_VirtualProtect)(LPVOID, SIZE_T, DWORD, PDWORD) = nullptr;

    LONG CALLBACK VectoredHandler(PEXCEPTION_POINTERS ep)
    {
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT)
        {
            void* addr = ep->ExceptionRecord->ExceptionAddress;

            for (int i = 0; i < g_hookCount; i++)
            {
                if (g_hooks[i].target == addr)
                {
                    // Restore original byte at target (so the normal function can run)
                    DWORD old = 0;
                    g_VirtualProtect(g_hooks[i].target, 1, PAGE_EXECUTE_READWRITE, &old);
                    *g_hooks[i].target = g_hooks[i].origByte;
                    g_VirtualProtect(g_hooks[i].target, 1, old, &old);

                    // Redirect execution to the thunk
                    ep->ContextRecord->Rip = (uint64_t)g_hooks[i].thunk;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

} // anonymous namespace

namespace Int3Hook {

    bool InitVEH()
    {
        // Cache VirtualProtect from Stealth APIs (avoids IAT, survives PE header erase)
        const auto& apis = Stealth::GetApis();
        g_VirtualProtect = apis.VirtualProtect_
            ? apis.VirtualProtect_
            : VirtualProtect;

        if (!g_VirtualProtect) {
            LOG_MSG("Int3Hook", "InitVEH: VirtualProtect unavailable");
            return false;
        }

        g_vehHandle = AddVectoredExceptionHandler(1, VectoredHandler);
        if (!g_vehHandle) {
            LOG_MSG("Int3Hook", "InitVEH: AddVectoredExceptionHandler FAILED");
            return false;
        }

        LOG("Int3Hook", "InitVEH: VEH=%p VirtualProtect=%p", g_vehHandle, g_VirtualProtect);
        return true;
    }

    void ShutdownVEH()
    {
        if (g_vehHandle) {
            RemoveVectoredExceptionHandler(g_vehHandle);
            g_vehHandle = nullptr;
            LOG_MSG("Int3Hook", "VEH removed");
        }
    }

    bool Install(uint8_t* target, void* thunk)
    {
        if (g_hookCount >= 4) {
            LOG_MSG("Int3Hook", "Install: registry full");
            return false;
        }

        uint8_t orig = *target;

        // Write 0xCC (INT3) at the target entry
        DWORD old = 0;
        g_VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old);
        *target = 0xCC;
        g_VirtualProtect(target, 1, old, &old);

        g_hooks[g_hookCount] = { target, orig, thunk };
        g_hookCount++;

        LOG("Int3Hook", "Install: %p orig=0x%02X thunk=%p (total=%d)",
            target, orig, thunk, g_hookCount);
        return true;
    }

    void Uninstall(uint8_t* target)
    {
        for (int i = 0; i < g_hookCount; i++)
        {
            if (g_hooks[i].target == target)
            {
                auto& h = g_hooks[i];

                // Restore original byte (regardless of current state: 0xCC or origByte)
                DWORD old = 0;
                g_VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old);
                *target = h.origByte;
                g_VirtualProtect(target, 1, old, &old);

                LOG("Int3Hook", "Uninstall: %p byte=0x%02X", target, h.origByte);

                // Remove from registry (swap with last)
                g_hookCount--;
                if (i < g_hookCount)
                    g_hooks[i] = g_hooks[g_hookCount];
                return;
            }
        }

        LOG("Int3Hook", "Uninstall: %p not found", target);
    }

    void ReArm(uint8_t* target)
    {
        // Re-write 0xCC after the thunk completes
        DWORD old = 0;
        g_VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old);
        *target = 0xCC;
        g_VirtualProtect(target, 1, old, &old);
    }

} // namespace Int3Hook
