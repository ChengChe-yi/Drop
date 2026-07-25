#include "pch.h"
#include "Stealth.h"
#include "XorStr.h"
#include "Config.h"
#include "Logger.h"
#include <cstring>

// Manual NT structures (winternl.h definitions are incomplete)
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} MY_UNICODE_STRING, *PMY_UNICODE_STRING;

typedef struct _MY_PEB_LDR_DATA {
    ULONG Length;
    BOOLEAN Initialized;
    HANDLE SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} MY_PEB_LDR_DATA, *PMY_PEB_LDR_DATA;

typedef struct _MY_LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    ULONG SizeOfImage;
    MY_UNICODE_STRING FullDllName;
    MY_UNICODE_STRING BaseDllName;
} MY_LDR_DATA_TABLE_ENTRY, *PMY_LDR_DATA_TABLE_ENTRY;

namespace Stealth
{

// ============================================================================
// PEB walking helpers
// ============================================================================

static inline void* GetPeb()
{
    return (void*)__readgsqword(0x60);
}

static PMY_PEB_LDR_DATA GetLdr()
{
    void* peb = GetPeb();
    if (!peb) return nullptr;
    // PEB->Ldr is at offset 0x18 on x64
    return *(PMY_PEB_LDR_DATA*)((uint8_t*)peb + 0x18);
}

static HMODULE FindModule(const wchar_t* name)
{
    PMY_PEB_LDR_DATA ldr = GetLdr();
    if (!ldr) return nullptr;

    LIST_ENTRY* head = &ldr->InMemoryOrderModuleList;
    LIST_ENTRY* entry = head->Flink;

    while (entry != head)
    {
        auto* entryEx = CONTAINING_RECORD(entry, MY_LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
        if (entryEx->DllBase && entryEx->BaseDllName.Buffer)
        {
            if (_wcsicmp(entryEx->BaseDllName.Buffer, name) == 0)
                return (HMODULE)entryEx->DllBase;
        }
        entry = entry->Flink;
    }
    return nullptr;
}

static FARPROC FindExport(HMODULE base, const char* name)
{
    if (!base) return nullptr;
    uint8_t* addr = (uint8_t*)base;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)addr;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(addr + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY* exportDir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exportDir->Size || !exportDir->VirtualAddress) return nullptr;

    IMAGE_EXPORT_DIRECTORY* exports = (IMAGE_EXPORT_DIRECTORY*)(addr + exportDir->VirtualAddress);
    DWORD* names = (DWORD*)(addr + exports->AddressOfNames);
    WORD* ordinals = (WORD*)(addr + exports->AddressOfNameOrdinals);
    DWORD* functions = (DWORD*)(addr + exports->AddressOfFunctions);

    for (DWORD i = 0; i < exports->NumberOfNames; i++)
    {
        const char* exportName = (const char*)(addr + names[i]);
        if (strcmp(exportName, name) == 0)
        {
            DWORD funcRva = functions[ordinals[i]];
            if (funcRva >= exportDir->VirtualAddress && funcRva < exportDir->VirtualAddress + exportDir->Size)
                return nullptr;
            return (FARPROC)(addr + funcRva);
        }
    }
    return nullptr;
}

// ============================================================================
// API resolution
// ============================================================================

static Apis g_apis;

static void ResolveOne(const char* names[], FARPROC* targets[], int count, HMODULE mod)
{
    for (int i = 0; i < count; i++)
    {
        if (!*targets[i] && mod)
            *targets[i] = FindExport(mod, names[i]);
    }
}

void ResolveApis()
{
    HMODULE kernel32 = FindModule(XWSTR(L"kernel32.dll"));
    HMODULE ntdll = FindModule(XWSTR(L"ntdll.dll"));

    static const char* k32_names[] = {
        "LoadLibraryW", "GetProcAddress", "VirtualAlloc", "VirtualProtect",
        "VirtualFree", "CreateThread", "GetLastError", "Sleep", "CloseHandle",
        "GetModuleFileNameW", "GetModuleHandleW", "GetModuleFileNameA",
        "DisableThreadLibraryCalls", "CreateFileA", "GetFileTime", "VirtualQuery"
    };
    static FARPROC* k32_targets[] = {
        (FARPROC*)&g_apis.LoadLibraryW_, (FARPROC*)&g_apis.GetProcAddress_,
        (FARPROC*)&g_apis.VirtualAlloc_, (FARPROC*)&g_apis.VirtualProtect_,
        (FARPROC*)&g_apis.VirtualFree_, (FARPROC*)&g_apis.CreateThread_,
        (FARPROC*)&g_apis.GetLastError_, (FARPROC*)&g_apis.Sleep_,
        (FARPROC*)&g_apis.CloseHandle_, (FARPROC*)&g_apis.GetModuleFileNameW_,
        (FARPROC*)&g_apis.GetModuleHandleW_, (FARPROC*)&g_apis.GetModuleFileNameA_,
        (FARPROC*)&g_apis.DisableThreadLibraryCalls_, (FARPROC*)&g_apis.CreateFileA_,
        (FARPROC*)&g_apis.GetFileTime_, (FARPROC*)&g_apis.VirtualQuery_
    };

    for (int i = 0; i < sizeof(k32_names) / sizeof(const char*); i++)
    {
        if (!*k32_targets[i] && kernel32)
            *k32_targets[i] = FindExport(kernel32, k32_names[i]);
    }

    // ntdll
    if (ntdll)
        g_apis.NtProtectVirtualMemory_ = (LONG(NTAPI*)(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG))
            FindExport(ntdll, "NtProtectVirtualMemory");
}

const Apis& GetApis()
{
    if (!g_apis.LoadLibraryW_)
        ResolveApis();
    return g_apis;
}

// ============================================================================
// PEB hiding
// ============================================================================

void HideFromPEB()
{
    HMODULE thisMod = Config::g_hModule;
    if (!thisMod) return;

    PMY_PEB_LDR_DATA ldr = GetLdr();
    if (!ldr) return;

    LIST_ENTRY* head = &ldr->InMemoryOrderModuleList;
    LIST_ENTRY* entry = head->Flink;

    while (entry != head)
    {
        auto* entryEx = CONTAINING_RECORD(entry, MY_LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
        if (entryEx->DllBase == thisMod)
        {
            // Unlink from InMemoryOrderLinks
            entry->Blink->Flink = entry->Flink;
            entry->Flink->Blink = entry->Blink;

            // Unlink from InLoadOrderLinks
            entryEx->InLoadOrderLinks.Blink->Flink = entryEx->InLoadOrderLinks.Flink;
            entryEx->InLoadOrderLinks.Flink->Blink = entryEx->InLoadOrderLinks.Blink;

            // Unlink from InitializationOrderLinks
            entryEx->InInitializationOrderLinks.Blink->Flink = entryEx->InInitializationOrderLinks.Flink;
            entryEx->InInitializationOrderLinks.Flink->Blink = entryEx->InInitializationOrderLinks.Blink;
            break;
        }
        entry = entry->Flink;
    }
}

void Init()
{
    ResolveApis();
}


void ErasePEHeader()
{

    HMODULE thisMod = Config::g_hModule;
    if (!thisMod) return;

    uint8_t* base = (uint8_t*)thisMod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return; 

    size_t eraseBytes = sizeof(IMAGE_DOS_HEADER) + sizeof(uint32_t);  
    const Apis& apis = GetApis();
    DWORD oldProt = 0;
    auto pVirtualProtect = apis.VirtualProtect_ ? apis.VirtualProtect_ : VirtualProtect;
    if (!pVirtualProtect(base, eraseBytes, PAGE_READWRITE, &oldProt)) {
        LOG_MSG("Stealth", "ErasePEHeader: VirtualProtect(RW) failed");
        return;
    }
    memset(base, 0, eraseBytes);
    pVirtualProtect(base, eraseBytes, oldProt, &oldProt);
    LOG_MSG("Stealth", "PE headers erased");
}

} // namespace Stealth
