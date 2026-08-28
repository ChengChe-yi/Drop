#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include "Config.h"
#include "Logger.h"
#include "PickupSuppress.h"

// DllMain 只做最小工作：存句柄、建事件、建 worker 线程。
// loader lock 期间不做文件 IO / VirtualProtect / 子线程创建，
// 全部初始化与清理都在 worker 中执行。
namespace
{
    HMODULE g_hModule = nullptr;
    HANDLE  g_worker  = nullptr;
    HANDLE  g_stop    = nullptr;

    DWORD WINAPI WorkerProc(LPVOID)
    {
        Logger::InitLogFile(g_hModule);
        Config::StartHotReload();

        if (Hooks::Init())
            LOG_MSG("Drop", "Plugin loaded");
        else
            LOG_MSG("Drop", "Hook init failed");

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
        // 仅 FreeLibrary 动态卸载时清理；进程退出路径不做事，避免拆除期崩溃。
        if (lpReserved == nullptr)
        {
            if (g_stop && g_worker) {
                SetEvent(g_stop);
                WaitForSingleObject(g_worker, 3000);
            }
        }
        break;
    }
    return TRUE;
}
