#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include "Config.h"
#include "Logger.h"

namespace
{
    HMODULE g_hModule = nullptr;
    HANDLE  g_worker  = nullptr;
    HANDLE  g_stop    = nullptr;

    static constexpr DWORD kWorkerStopWaitMs = 5000;

    DWORD WINAPI WorkerProc(LPVOID)
    {
        Logger::InitLogFile(g_hModule);
        Config::StartHotReload();

        if (Hooks::Init())
            LOG_MSG("InteeKit", "Plugin loaded");
        else
            LOG_MSG("InteeKit", "Hook init failed");

        WaitForSingleObject(g_stop, INFINITE);

        Config::StopHotReload();
        Hooks::Uninit();
        Logger::CloseLog();
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD   ul_reason_for_call,
                      LPVOID  lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;

        {
            HMODULE pinned = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN |
                                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                    (LPCWSTR)hModule, &pinned))
                return FALSE;
        }

        g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_stop)
            return FALSE;

        g_worker = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
        if (!g_worker) {
            CloseHandle(g_stop);
            g_stop = nullptr;
            return FALSE;
        }
        break;

    case DLL_PROCESS_DETACH:
        if (g_stop) {
            SetEvent(g_stop);
            if (g_worker) {
                WaitForSingleObject(g_worker, kWorkerStopWaitMs);
                CloseHandle(g_worker);
                g_worker = nullptr;
            }
            CloseHandle(g_stop);
            g_stop = nullptr;
        }
        break;
    }
    return TRUE;
}
