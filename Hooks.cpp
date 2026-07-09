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

// ===== 模块基址 =====
static uintptr_t g_base = 0;
static void EnsureBase()
{
    if (g_base) return;
    HMODULE m = GetModuleHandleW(L"YuanShen.exe");
    g_base = m ? (uintptr_t)m : 0;
}

// ===== 函数指针 =====
typedef int64_t(__fastcall* Sub1C40880_t)(int64_t* a1, unsigned int a2);
typedef int64_t(__fastcall* Sub1B81160_t)(int64_t a1, unsigned int a2, int64_t a3, int64_t a4);
typedef int64_t(__fastcall* SubDFF010_t)(int64_t a1, int64_t a2, int a3, char a4, int64_t a5);
typedef void* (WINAPI* GetName_t)(void*, int);

static Sub1C40880_t  o_1C40880 = nullptr;
static Sub1B81160_t  o_1B81160 = nullptr;
static SubDFF010_t   o_DFF010  = nullptr;
static GetName_t     o_getName = nullptr;

// ===== TLS =====
thread_local wchar_t g_itemName[64] = {0};

// ===== 工具函数 =====

/// 名字是否匹配黑名单
static bool IsBlacklisted(const wchar_t* name, const DropConfig& cfg)
{
    if (!name || name[0] == 0) return false;
    if (!cfg.pillarFilterEnabled) return false;

    // 只处理 SceneObj_DropItem 前缀, 其他一律放行
    if (wcsstr(name, L"SceneObj_DropItem") != name) return false;

    // 圣遗物自动放行
    if (wcsstr(name, L"SceneObj_DropItem_Relic_Lv") == name) return false;

    for (auto& n : cfg.filterNames)
    {
        if (wcscmp(name, n.c_str()) == 0)
            return cfg.isBlacklist;
    }
    return !cfg.isBlacklist;
}

/// 尝试读类型+名字 (纯C, 可用 __try)
static bool TryReadItemInfo(int64_t* a1, int& outType, wchar_t* name, int nameMax)
{
    outType = 0;
    name[0] = 0;
    if (!a1) return false;

    __try { outType = *(int*)((uintptr_t)a1 + Offsets::kTypeFieldOffset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    if (outType != Offsets::kRelicTypeValue) return false;

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

// ═════════════════════════════════════════════════════
//  Hook: sub_154DFF010 — 拾取提示框
//  当 suppressPickupBar=false + 黑名单命中 → 返回0
// ═════════════════════════════════════════════════════
static int64_t __fastcall SubDFF010Hook(int64_t a1, int64_t a2, int a3, char a4, int64_t a5)
{
    if (g_itemName[0])
    {
        DropConfig cfg = Config::Get();
        if (!cfg.suppressPickupBar && IsBlacklisted(g_itemName, cfg))
        {
            LOG("DFF010 SUPPRESS pickup bar for '%S'", g_itemName);
            return 0;
        }
    }
    return o_DFF010(a1, a2, a3, a4, a5);
}

// ═════════════════════════════════════════════════════
//  Hook: sub_151C40880 — 光柱创建
//  当 suppressPickupBar=true + 黑名单命中 → 返回0
//  否则正常创建光柱, 拾取提示框由 DFF010 决定
// ═════════════════════════════════════════════════════
static int64_t __fastcall Sub1C40880Hook(int64_t* a1, unsigned int a2)
{
    g_itemName[0] = 0;
    DropConfig cfg = Config::Get();

    int typeVal = 0;
    TryReadItemInfo(a1, typeVal, g_itemName, 64);

    if (typeVal == Offsets::kRelicTypeValue && g_itemName[0])
        LOG("C40880 type=17 name='%S'", g_itemName);

    // suppressPickupBar=true → 在这里直接决定屏蔽光柱
    if (typeVal == Offsets::kRelicTypeValue && g_itemName[0]
        && cfg.suppressPickupBar && IsBlacklisted(g_itemName, cfg))
    {
        LOG("C40880 SUPPRESS pillar+bar for '%S'", g_itemName);
        return 0;
    }

    // 否则正常创建 (光柱保留, 拾取栏由 DFF010 决定)
    return o_1C40880(a1, a2);
}

// ═════════════════════════════════════════════════════
//  Hook: sub_151B81160 (透传)
// ═════════════════════════════════════════════════════
static int64_t __fastcall Sub1B81160Hook(int64_t a1, unsigned int a2, int64_t a3, int64_t a4)
{
    return o_1B81160(a1, a2, a3, a4);
}

// ═════════════════════════════════════════════════════
//  Hook 安装
// ═════════════════════════════════════════════════════
static bool InstallHook(const char* name, uintptr_t offset, void* detour, void** orig)
{
    EnsureBase();
    if (!g_base) { LOG("%s: base=0", name); return false; }

    void* target = (void*)(g_base + offset);
    MH_STATUS s = MH_CreateHook(target, detour, orig);
    if (s != MH_OK) { LOG("%s: MH_CreateHook=%d", name, s); return false; }

    s = MH_EnableHook(target);
    if (s != MH_OK) { LOG("%s: MH_EnableHook=%d", name, s); return false; }

    LOG("%s OK @ 0x%llX", name, (uintptr_t)target);
    return true;
}

namespace Hooks
{
    bool Init()
    {
        MH_Initialize();
        EnsureBase();
        if (!g_base) { LOG("Can't find YuanShen.exe"); return false; }

        o_getName = (GetName_t)(g_base + Offsets::kGetName);

        InstallHook("C40880", Offsets::kSub151C40880, &Sub1C40880Hook, (void**)&o_1C40880);
        InstallHook("DFF010", Offsets::kSub154DFF010, &SubDFF010Hook,  (void**)&o_DFF010);
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
        o_DFF010  = nullptr;
        o_getName = nullptr;
        LOG("All hooks uninstalled");
    }
}
