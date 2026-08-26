#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "PickupSuppress.h"
#include "Patterns.h"
#include "Config.h"
#include "Logger.h"
#include "Whitelist.h"
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

    Config::Tick();
    Whitelist::Tick();

    char iconUtf8[64] = {}, nameUtf8[64] = {};
    bool shouldBlock = false;

    __try {
        if (a2 && Config::g_pillarFilterEnabled && Config::g_pickupSuppressEnabled) {
            ReadStringUtf8SEH(*(__int64*)(a2 + 0x28), iconUtf8, 64);
            ReadStringUtf8SEH(*(__int64*)(a2 + 0x18), nameUtf8, 64);

            LOG("PDhook", "icon='%s' name='%s' (depth=%d)", iconUtf8, nameUtf8, g_depth);

            if (iconUtf8[0] && strstr(iconUtf8, "UI_ItemIcon_112")) {
                shouldBlock = true;
            }

            if (shouldBlock && nameUtf8[0] && Whitelist::IsPickupAllowed(nameUtf8)) {
                LOG("PDhook", "WHITELIST: '%s'", nameUtf8);
                shouldBlock = false;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG("PDhook", "EXCEPTION reading a2");
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
        // 主模块即注入目标，不依赖具体 exe 名
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
