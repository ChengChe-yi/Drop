#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Hooks.h"
#include "Config.h"
#include "Logger.h"

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

        // 注入型插件不支持动态卸载
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
        break;
    }
    return TRUE;
}
