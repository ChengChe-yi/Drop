#include "pch.h"
#include "Hooks.h"
#include "Config.h"
#include "Logger.h"
#include "Stealth.h"
#include "PickupSuppress.h"
#include <atomic>

namespace Config { HMODULE g_hModule = nullptr; }

static std::atomic<bool> g_initDone{false};

void RunDelayedInit()
{
    if (g_initDone.load(std::memory_order_acquire)) return;
    g_initDone.store(true, std::memory_order_release);

    Stealth::HideFromPEB();

    LOG_MSG("Drop", "Drop Plugin Loaded");

    Config::StartHotReload();

    Hooks::Init();

    LOG_MSG("Drop", "Hooks installed via RunDelayedInit");

    Stealth::ErasePEHeader();
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
        Stealth::Init();
        InitLogFile(hModule);
        Hooks::Init();
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
