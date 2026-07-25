#include "pch.h"
#include "PillarSuppress.h"
#include "Config.h"
#include "Patterns.h"
#include "Logger.h"
#include "XorStr.h"
#include <cstring>

static uint8_t* g_target = nullptr;
static uint8_t  g_origBytes[14] = {};
static uint8_t  g_jmpBytes[14] = {};
static bool     g_hooked = false;

typedef __int64(__fastcall* tOriginal)(__int64, unsigned int, __int64, __int64);
typedef __int64(__fastcall* tGetName)(__int64, __int64);

static tGetName GetNameFn(uint8_t* base)
{
    static tGetName fn = nullptr;
    if (!fn)
        fn = (tGetName)(base + Offsets::RVA::GetName);
    return fn;
}

static bool IsDropItemMonster(__int64 np)
{
    wchar_t buf[256] = {};
    int nameLen = 0;
    __try {
        nameLen = *(uint16_t*)(np + 0x10);
        if (nameLen > 0 && nameLen < 250) {
            memcpy(buf, (void*)(np + 0x14), nameLen * sizeof(wchar_t));
            buf[nameLen] = 0;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return (nameLen > 0 && XWCSSTR(buf, L"SceneObj_DropItem_Monster"));
}

static void BuildJmpBytes(uint8_t* dest, void* jmpTarget)
{
    dest[0] = 0xFF; dest[1] = 0x25;
    dest[2] = 0x00; dest[3] = 0x00; dest[4] = 0x00; dest[5] = 0x00;
    *(void**)(dest + 6) = jmpTarget;
}

static __int64 CallOriginal(__int64 a1, unsigned int a2, __int64 a3, __int64 a4)
{
    DWORD old = 0;
    VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_target, g_origBytes, 14);
    VirtualProtect(g_target, 14, old, &old);

    auto result = ((tOriginal)g_target)(a1, a2, a3, a4);

    VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_target, g_jmpBytes, 14);
    VirtualProtect(g_target, 14, old, &old);

    return result;
}

static __int64 __fastcall B81160_Thunk(__int64 a1, unsigned int a2, __int64 a3, __int64 a4)
{
    char nameUtf8[256] = {};
    bool shouldBlock = false;

    auto gn = GetNameFn((uint8_t*)GetModuleHandleW(nullptr));
    if (gn && Config::g_pillarFilterEnabled && Config::g_pillarSuppressEnabled) {
        __int64 np = 0;
        __try { np = gn(a3, 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        if (np && IsDropItemMonster(np)) {
            int nameLen = 0;
            __try { nameLen = *(uint16_t*)(np + 0x10); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            if (nameLen > 0) {
                WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)(np + 0x14), nameLen, nameUtf8, 254, nullptr, nullptr);
            }
            shouldBlock = true;
        }
    }

    if (shouldBlock) {
        LOG("Pillar", "BLOCK: '%s' type=%u", nameUtf8, a2);
        return 0;
    }

    return CallOriginal(a1, a2, a3, a4);
}

static bool DoInit()
{
    uint8_t* base = (uint8_t*)GetModuleHandleW(nullptr);
    if (!base) return false;

    g_target = base + Offsets::RVA::B81160_RVA;

    memcpy(g_origBytes, g_target, 14);
    BuildJmpBytes(g_jmpBytes, B81160_Thunk);

    LOG("Pillar", "B81160 entry = %llX, first bytes %02X %02X %02X %02X",
        (uint64_t)g_target, g_origBytes[0], g_origBytes[1], g_origBytes[2], g_origBytes[3]);

    DWORD old = 0;
    VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_target, g_jmpBytes, 14);
    VirtualProtect(g_target, 14, old, &old);

    g_hooked = true;
    LOG_MSG("Pillar", "FF 25 JMP hook installed OK");
    return true;
}

bool PillarSuppress::Init()
{
    __try { return DoInit(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_MSG("Pillar", "Init EXCEPTION!");
        return false;
    }
}

void PillarSuppress::Uninit()
{
    if (g_hooked && g_target) {
        DWORD old = 0;
        VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old);
        memcpy(g_target, g_origBytes, 14);
        VirtualProtect(g_target, 14, old, &old);
        g_hooked = false;
        LOG_MSG("Pillar", "Uninit OK (bytes restored)");
    }
}
