#include "pch.h"
#include "Hooks.h"
#include "Config.h"

DWORD WINAPI InitThread(LPVOID)
{
    Sleep(2000);
    OutputDebugStringA("[Drop] Installing hooks...\n");

    Config::StartHotReload();

    if (Hooks::Init())
        OutputDebugStringA("[Drop] Hooks installed!\n");
    else
        OutputDebugStringA("[Drop] Hooks FAILED!\n");

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD   ul_reason_for_call,
                      LPVOID  lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        {
            HANDLE hThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
            if (hThread) CloseHandle(hThread);
        }
        break;

    case DLL_PROCESS_DETACH:
        if (lpReserved == nullptr)
        {
            Config::StopHotReload();
            Hooks::Uninit();
        }
        break;
    }
    return TRUE;
}
