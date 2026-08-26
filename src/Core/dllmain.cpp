#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include "Config.h"
#include "Logger.h"
#include "PickupSuppress.h"

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD   ul_reason_for_call,
                      LPVOID  lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Logger::InitLogFile(hModule);
        Config::StartHotReload();
        Hooks::Init();
        LOG_MSG("Drop", "Plugin loaded");
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
