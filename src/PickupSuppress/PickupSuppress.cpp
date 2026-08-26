#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "PickupSuppress.h"
#include "Patterns.h"
#include "Config.h"
#include "Logger.h"
#include "Whitelist.h"
#include <atomic>
#include <cstdint>
#include <cstring>

static uint8_t* g_base = nullptr;
static uint8_t* g_target = nullptr;
static uint8_t  g_origBytes[14] = {};
static uint8_t  g_jmpBytes[14] = {};
static bool     g_hooked = false;

typedef __int64(__fastcall* tOriginal)(__int64, __int64);

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

static bool ReadStringUtf8SEH(__int64 p, char* out, int outSize)
{
    if (!p || (size_t)p < 0x10000) return false;
    __try {
        Il2CppString* str = (Il2CppString*)p;
        if (str->length <= 0 || str->length >= 100) return false;
        int cl = (str->length < 30) ? str->length : 30;
        WideCharToMultiByte(CP_UTF8, 0, str->chars, cl, out, outSize - 1, nullptr, nullptr);
        out[outSize - 1] = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void BuildJmpBytes(uint8_t* dest, void* jmpTarget)
{
    dest[0] = 0xFF;
    dest[1] = 0x25;
    dest[2] = 0x00;
    dest[3] = 0x00;
    dest[4] = 0x00;
    dest[5] = 0x00;
    *(void**)(dest + 6) = jmpTarget;
}

static __int64 CallOriginal(__int64 a1, __int64 a2)
{
    DWORD old = 0;
    VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_target, g_origBytes, 14);
    VirtualProtect(g_target, 14, old, &old);

    auto result = ((tOriginal)g_target)(a1, a2);

    VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_target, g_jmpBytes, 14);
    VirtualProtect(g_target, 14, old, &old);

    return result;
}

static __int64 __fastcall PD_Handler(__int64 a1, __int64 a2)
{
    static thread_local int g_depth = 0;
    g_depth++;

    // Hot-reload polling — also driven by the background timer thread, but
    // we call it here too so that during gameplay (the common case) file
    // edits are picked up within milliseconds rather than up to 500ms. The
    // 1-second throttle inside Tick deduplicates the work.
    Config::Tick();
    Whitelist::Tick();

    // Cheap out when the master switch is off — every pickup in the game
    // flows through this function, so skipping the whole body is the single
    // biggest win when the user has disabled pickup suppression.
    if (!a2 || !Config::g_pickupSuppressEnabled.load(std::memory_order_acquire)) {
        g_depth--;
        return CallOriginal(a1, a2);
    }

    char iconUtf8[64] = {};
    char nameUtf8[64] = {};
    bool shouldBlock = false;

    __try {
        // Only convert the icon (and only check the icon prefix). The name
        // field is deferred — for the 99% of pickups that aren't
        // `UI_ItemIcon_112`, this skips a WideCharToMultiByte call.
        ReadStringUtf8SEH(*(__int64*)(a2 + 0x28), iconUtf8, 64);

        if (iconUtf8[0] && strstr(iconUtf8, "UI_ItemIcon_112")) {
            ReadStringUtf8SEH(*(__int64*)(a2 + 0x18), nameUtf8, 64);

            if (!Whitelist::IsPickupAllowed(nameUtf8))
                shouldBlock = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silent on SEH — verbose per-call logging in this hot path costs
        // more than it's worth.
    }

    if (shouldBlock) {
        LOG("PDhook", "BLOCK icon='%s' name='%s'", iconUtf8, nameUtf8);
        g_depth--;
        return 0;
    }

    auto result = CallOriginal(a1, a2);
    g_depth--;
    return result;
}

static bool DoInit()
{
    if (!g_base)
        g_base = (uint8_t*)GetModuleHandleW(nullptr);
    if (!g_base) return false;

    g_target = g_base + Offsets::RVA::PickupDataAdd;

    memcpy(g_origBytes, g_target, 14);

    BuildJmpBytes(g_jmpBytes, PD_Handler);

    LOG("PDhook", "target entry = %llX, orig = %02X %02X %02X %02X...",
        (uint64_t)g_target,
        g_origBytes[0], g_origBytes[1], g_origBytes[2], g_origBytes[3]);

    DWORD old = 0;
    VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_target, g_jmpBytes, 14);
    VirtualProtect(g_target, 14, old, &old);

    g_hooked = true;
    LOG_MSG("PDhook", "hook installed OK");
    return true;
}

bool PickupSuppress::Init()
{
    __try { return DoInit(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_MSG("PDhook", "Init EXCEPTION!");
        return false;
    }
}

void PickupSuppress::Uninit()
{
    if (g_hooked && g_target) {
        DWORD old = 0;
        VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old);
        memcpy(g_target, g_origBytes, 14);
        VirtualProtect(g_target, 14, old, &old);
        g_hooked = false;
        LOG_MSG("PDhook", "Uninit OK (bytes restored)");
    }
}
