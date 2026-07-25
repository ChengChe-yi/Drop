#include "pch.h"
#include "PillarSuppress.h"
#include "Hooks.h"
#include "Config.h"
#include "Patterns.h"
#include "Logger.h"
#include "XorStr.h"
#include "Int3Hook.h"

extern void RunDelayedInit();

#include <cstring>

static uint8_t* g_base = nullptr;
static uint8_t* g_b81160_entry = nullptr;

typedef __int64(__fastcall* tGetName)(__int64 a1, __int64 a2);
typedef __int64(__fastcall* tB81160)(__int64, unsigned int, __int64, __int64);

static tGetName GetNameFn()
{
    static tGetName fn = nullptr;
    if (!fn)
        fn = (tGetName)(g_base + Offsets::RVA::GetName);
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

static __int64 __fastcall B81160_Thunk(__int64 a1, unsigned int a2, __int64 a3, __int64 a4)
{
    RunDelayedInit();
    Config::Tick();

    char nameUtf8[256] = {};
    bool shouldBlock = false;

    auto gn = GetNameFn();
    if (gn && Config::g_pillarFilterEnabled && Config::g_pillarSuppressEnabled) {
        __int64 np = 0;
        __try {
            np = gn(a3, 0);
        }
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
        Int3Hook::ReArm(g_b81160_entry);
        return 0;
    }

    auto result = ((tB81160)g_b81160_entry)(a1, a2, a3, a4);
    Int3Hook::ReArm(g_b81160_entry);
    return result;
}

static bool DoInit()
{
    g_base = (uint8_t*)GetModuleHandleW(XWSTR(L"YuanShen.exe"));
    if (!g_base)
        g_base = (uint8_t*)GetModuleHandleW(nullptr);
    if (!g_base) return false;

    g_b81160_entry = g_base + Offsets::RVA::B81160_RVA;
    LOG("Pillar", "B81160 entry = %llX, first byte = %02X",
        (uint64_t)g_b81160_entry, *g_b81160_entry);

    return Int3Hook::Install(g_b81160_entry, (void*)B81160_Thunk);
}

bool PillarSuppress::Init()
{
    __try { return DoInit(); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LOG_MSG("Pillar", "Init EXCEPTION!");
        return false;
    }
}

void PillarSuppress::Uninit()
{
    if (g_b81160_entry) {
        Int3Hook::Uninstall(g_b81160_entry);
        g_b81160_entry = nullptr;
    }
    g_base = nullptr;
    LOG_MSG("Pillar", "Uninit OK");
}
