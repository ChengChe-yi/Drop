#include "pch.h"
#include "Hooks.h"
#include "Patterns.h"
#include "Config.h"
#include <MinHook.h>

#define LOG(fmt, ...) do {                          \
    char _b_[512];                                  \
    sprintf_s(_b_, "[Drop] " fmt "\n", __VA_ARGS__);\
    OutputDebugStringA(_b_);                        \
} while(0)

static uintptr_t g_base = 0;
static void EnsureBase()
{
    if (g_base) return;
    HMODULE m = GetModuleHandleW(L"YuanShen.exe");
    g_base = m ? (uintptr_t)m : 0;
}

typedef int64_t(__fastcall* Sub1C40880_t)(int64_t* a1, unsigned int a2);
typedef int64_t(__fastcall* Sub1B81160_t)(int64_t a1, unsigned int a2, int64_t a3, int64_t a4);
typedef void* (WINAPI* GetName_t)(void*, int);

static Sub1C40880_t  o_1C40880 = nullptr;
static Sub1B81160_t  o_1B81160 = nullptr;
static GetName_t     o_getName = nullptr;

thread_local wchar_t g_itemName[64] = {0};

static bool ShouldSuppress(const wchar_t* name, const DropConfig& cfg)
{
    if (!name || !name[0]) return false;
    if (!cfg.pillarFilterEnabled) return false;
    if (wcsstr(name, L"SceneObj_DropItem") != name) return false;
    if (wcsstr(name, L"SceneObj_DropItem_Relic_Lv") == name) return false;

    for (auto& n : cfg.filterNames)
    {
        if (wcscmp(name, n.c_str()) == 0)
            return cfg.isBlacklist;
    }
    return !cfg.isBlacklist;
}

static bool ReadItemInfo(int64_t* a1, int& outType, wchar_t* name, int nameMax)
{
    outType = 0; name[0] = 0;
    if (!a1) return false;

    __try { outType = *(int*)((uintptr_t)a1 + Offsets::kTypeFieldOffset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    __try {
        void* obj = (void*)a1[Offsets::kObjFieldIndex];
        if (obj && o_getName) {
            void* ns = o_getName(obj, 0);
            if (ns) {
                int len = *(uint16_t*)((uintptr_t)ns + 0x10);
                if (len > 0 && len < nameMax - 2) {
                    memcpy(name, (void*)((uintptr_t)ns + 0x14), len * sizeof(wchar_t));
                    name[len] = 0;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return true;
}

static int64_t __fastcall Sub1C40880Hook(int64_t* a1, unsigned int a2)
{
    g_itemName[0] = 0;
    DropConfig cfg = Config::Get();
    int typeVal = 0;

    ReadItemInfo(a1, typeVal, g_itemName, 64);

    if (g_itemName[0] && wcsstr(g_itemName, L"SceneObj_DropItem") == g_itemName)
        LOG("C40880 type=%d name='%S'", typeVal, g_itemName);

    if (typeVal == Offsets::kRelicTypeValue && g_itemName[0]
        && cfg.suppressPickupBar && ShouldSuppress(g_itemName, cfg))
    {
        LOG("C40880 SUPPRESS pillar+bar for '%S'", g_itemName);
        return 0;
    }
    return o_1C40880(a1, a2);
}

static void ReadNameFromGameObject(int64_t ptr, wchar_t* out, int max)
{
    out[0] = 0;
    if (ptr <= 0x10000 || !o_getName) return;

    __try {
        void* ns = o_getName((void*)ptr, 0);
        if (ns) {
            int len = *(uint16_t*)((uintptr_t)ns + 0x10);
            if (len > 0 && len < max) {
                memcpy(out, (void*)((uintptr_t)ns + 0x14), len * sizeof(wchar_t));
                out[len] = 0;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int64_t __fastcall Sub1B81160Hook(int64_t a1, unsigned int a2, int64_t a3, int64_t a4)
{
    wchar_t nameBuf[64] = {0};
    ReadNameFromGameObject(a3, nameBuf, 64);

    if (nameBuf[0] && wcsstr(nameBuf, L"SceneObj_DropItem") == nameBuf)
        LOG("B81160 name='%S'", nameBuf);

    DropConfig cfg = Config::Get();
    if (nameBuf[0] && wcsstr(nameBuf, L"SceneObj_DropItem") == nameBuf
        && !cfg.suppressPickupBar && ShouldSuppress(nameBuf, cfg))
    {
        LOG("B81160 SUPPRESS pickup bar for '%S'", nameBuf);
        return 0;
    }
    return o_1B81160(a1, a2, a3, a4);
}

static bool InstallHook(const char* name, uintptr_t offset, void* detour, void** orig)
{
    EnsureBase();
    if (!g_base) { LOG("%s: base=0", name); return false; }

    void* target = (void*)(g_base + offset);
    MH_STATUS s = MH_CreateHook(target, detour, orig);
    if (s != MH_OK) { LOG("%s: CreateHook=%d", name, s); return false; }

    s = MH_EnableHook(target);
    if (s != MH_OK) { LOG("%s: EnableHook=%d", name, s); return false; }

    LOG("%s OK @ 0x%llX", name, (uintptr_t)target);
    return true;
}

namespace Hooks
{
    bool Init()
    {
        MH_Initialize();
        EnsureBase();
        if (!g_base) { LOG("YuanShen.exe not found"); return false; }

        o_getName = (GetName_t)(g_base + Offsets::kGetName);
        InstallHook("C40880", Offsets::kSub151C40880, &Sub1C40880Hook, (void**)&o_1C40880);
        InstallHook("B81160", Offsets::kSub151B81160, &Sub1B81160Hook, (void**)&o_1B81160);

        LOG("All hooks installed");
        return true;
    }

    void Uninit()
    {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        o_1C40880 = nullptr;
        o_1B81160 = nullptr;
        o_getName = nullptr;
        LOG("All hooks uninstalled");
    }
}
