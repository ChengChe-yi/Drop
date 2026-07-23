#pragma once
#include <windows.h>
#include <cstdint>

namespace Stealth
{
    /// Initialize all stealth measures
    void Init();

    /// Remove this module from PEB's loader data tables
    void HideFromPEB();


    void ErasePEHeader();

    /// Runtime-resolved API table — avoids IAT entries
    struct Apis
    {
        // kernel32
        HMODULE(WINAPI* LoadLibraryW_)(LPCWSTR) = nullptr;
        FARPROC(WINAPI* GetProcAddress_)(HMODULE, LPCSTR) = nullptr;
        void* (WINAPI* VirtualAlloc_)(void*, SIZE_T, DWORD, DWORD) = nullptr;
        BOOL (WINAPI* VirtualProtect_)(void*, SIZE_T, DWORD, PDWORD) = nullptr;
        BOOL (WINAPI* VirtualFree_)(void*, SIZE_T, DWORD) = nullptr;
        HANDLE(WINAPI* CreateThread_)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD) = nullptr;
        DWORD (WINAPI* GetLastError_)() = nullptr;
        void (WINAPI* Sleep_)(DWORD) = nullptr;
        BOOL (WINAPI* CloseHandle_)(HANDLE) = nullptr;
        BOOL (WINAPI* GetModuleFileNameW_)(HMODULE, LPWSTR, DWORD) = nullptr;
        HMODULE(WINAPI* GetModuleHandleW_)(LPCWSTR) = nullptr;
        DWORD (WINAPI* GetModuleFileNameA_)(HMODULE, LPSTR, DWORD) = nullptr;
        BOOL (WINAPI* DisableThreadLibraryCalls_)(HMODULE) = nullptr;
        HANDLE(WINAPI* CreateFileA_)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = nullptr;
        BOOL (WINAPI* GetFileTime_)(HANDLE, LPFILETIME, LPFILETIME, LPFILETIME) = nullptr;
        BOOL (WINAPI* VirtualQuery_)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) = nullptr;

        // ntdll
        LONG(NTAPI* NtProtectVirtualMemory_)(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG) = nullptr;
    };

    const Apis& GetApis();
    void ResolveApis();
}
