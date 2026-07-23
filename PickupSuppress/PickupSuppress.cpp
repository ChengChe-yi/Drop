#include "pch.h"
#include "PickupSuppress.h"
#include "Hooks.h"
#include "Patterns.h"
#include "Config.h"
#include "Logger.h"
#include "XorStr.h"
#include <cstring>

// ============================================================================
// PickupSuppress — 拾取提示框屏蔽 (INT3+VEH)
// Hook PickupDataAdd 入口：拦截怪物掉落物拾取提示框
// 检查 icon 字段是否以 UI_ItemIcon_112 开头，是则拦截
// ============================================================================

static uint8_t* g_base = nullptr;
static uint8_t* g_pd_entry = nullptr;

typedef __int64(__fastcall* tPickupData)(__int64, __int64);

// IL2CPP string
#pragma pack(push, 4)
struct Il2CppObject {
    void* klass;
    void* monitor;
};
#pragma pack(pop)

struct Il2CppString {
    Il2CppObject object;
    int32_t length;
    wchar_t chars[1];
};

// ============================================================================
// Base address helper
// ============================================================================
static void EnsureBase()
{
    if (g_base) return;
    g_base = (uint8_t*)GetModuleHandleW(L"YuanShen.exe");
    if (!g_base)
        g_base = (uint8_t*)GetModuleHandleW(nullptr);
}

// ============================================================================
// PD_Thunk — called by VEH when INT3 is hit at PickupDataAdd entry
// Arguments arrive in rcx, rdx (original PickupDataAdd args)
//
// Block path: ReArm, return 0
// Proceed path: call orig at g_pd_entry, ReArm, return orig's result
// ============================================================================
static __int64 __fastcall PD_Thunk(__int64 a1, __int64 a2)
{
    static thread_local int g_depth = 0;
    g_depth++;
    LOG("PDhook", "tid=%04x depth=%d", GetCurrentThreadId(), g_depth);

    // Lazy config hot-reload poll (runs on the game thread)
    Config::Tick();

    char iconUtf8[64] = {}, nameUtf8[64] = {};
    bool shouldBlock = false;

    __try {
        if (a2 && Config::g_pillarFilterEnabled && Config::g_pickupSuppressEnabled) {
            // Read icon (offset 0x30)
            Il2CppString* icon = *(Il2CppString**)(a2 + 0x30);
            if (icon && icon->chars && icon->length > 0 && icon->length < 100) {
                int cl = (icon->length < 30) ? icon->length : 30;
                WideCharToMultiByte(CP_UTF8, 0, icon->chars, cl, iconUtf8, 62, nullptr, nullptr);
            }

            // Read name (offset 0x20)
            __try {
                Il2CppString* nm = *(Il2CppString**)(a2 + 0x20);
                if (nm && nm->chars && nm->length > 0 && nm->length < 50) {
                    int cl = (nm->length < 30) ? nm->length : 30;
                    WideCharToMultiByte(CP_UTF8, 0, nm->chars, cl, nameUtf8, 62, nullptr, nullptr);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            if (XSTRSTR(iconUtf8, "UI_ItemIcon_112")) {
                shouldBlock = true;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (shouldBlock) {
        LOG("PDhook", "BLOCK icon='%s' name='%s'", iconUtf8, nameUtf8);
        Int3Hook::ReArm(g_pd_entry);
        return 0;
    }

    // Proceed: call the original function (entry is now intact)
    auto result = ((tPickupData)g_pd_entry)(a1, a2);
    Int3Hook::ReArm(g_pd_entry);
    return result;
}

// ============================================================================
// Init — install INT3 at PickupDataAdd entry
// ============================================================================
static bool DoInit()
{
    EnsureBase();
    if (!g_base) return false;

    g_pd_entry = g_base + Offsets::RVA::PickupDataAdd;
    LOG("PDhook", "PickupDataAdd entry = %llX, first byte = %02X",
        (uint64_t)g_pd_entry, *g_pd_entry);

    return Int3Hook::Install(g_pd_entry, (void*)PD_Thunk);
}

// ============================================================================
// Interface
// ============================================================================
bool PickupSuppress::Init()
{
    __try { return DoInit(); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LOG_MSG("PDhook", "Init EXCEPTION!");
        return false;
    }
}

void PickupSuppress::Uninit()
{
    if (g_pd_entry) {
        Int3Hook::Uninstall(g_pd_entry);
        g_pd_entry = nullptr;
    }
    g_base = nullptr;
    LOG_MSG("PDhook", "Uninit OK");
}
