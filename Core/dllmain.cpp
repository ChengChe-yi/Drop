#include "pch.h"
#include "Hooks.h"
#include "Config.h"
#include "Logger.h"
#include "Stealth.h"
#include <atomic>

namespace Config { HMODULE g_hModule = nullptr; }

// Delayed init — runs from first B81160 handler call (game thread, no extra thread)
static std::atomic<bool> g_initDone{false};

void RunDelayedInit()
{
    if (g_initDone.load(std::memory_order_acquire)) return;
    g_initDone.store(true, std::memory_order_release);

    Stealth::Init();
    Stealth::HideFromPEB();

    HMODULE hMod = Config::g_hModule;
    InitLogFile(hMod);
    LOG_MSG("Drop", "Drop Plugin Loaded");

    Config::StartHotReload();

    LOG_MSG("Drop", "Hooks already installed in DllMain");

    Stealth::ErasePEHeader();
}

extern "C" __declspec(dllexport) LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam)
{
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD   ul_reason_for_call,
                      LPVOID  lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        Config::g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);

        // Install hooks directly in DllMain (safe: VirtualAlloc/VirtualProtect only)
        // No thread creation, no file I/O, no config thread
        if (Hooks::Init())
            /* hooks installed */;
        break;

    case DLL_PROCESS_DETACH:
        if (lpReserved == nullptr)
        {
            Config::StopHotReload();
            Hooks::Uninit();
            CloseLog();
        }
        break;
    }
    return TRUE;
}
