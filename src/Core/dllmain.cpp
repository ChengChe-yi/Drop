#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include "Config.h"
#include "Logger.h"
#include "PickupSuppress.h"
#include <atomic>

static std::atomic<bool> g_initDone{false};

void RunDelayedInit()
{
    if (g_initDone.load(std::memory_order_acquire)) return;
    g_initDone.store(true, std::memory_order_release);

    LOG_MSG("Drop", "Drop Plugin Loaded");

    Config::StartHotReload();

    Hooks::Init();

    LOG_MSG("Drop", "Hooks installed via RunDelayedInit");
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
        Logger::InitLogFile(hModule);
        Hooks::Init();
        break;

    case DLL_PROCESS_DETACH:
        if (lpReserved == nullptr)
        {
            Config::StopHotReload();
            Hooks::Uninit();
            Logger::CloseLog();
        }
        break;
    }
    return TRUE;
}
