#include "pch.h"
#include "PillarSuppress.h"
#include "Config.h"
#include "Patterns.h"
#include "Logger.h"
#include "XorStr.h"
// Called from first B81160_Handler invocation (runs in game thread context)
// Declared in dllmain.cpp, called from PillarSuppress
extern void RunDelayedInit();

#include <cstring>

// ============================================================================
// PillarSuppress — 光柱屏蔽
// Hook B81160 函数入口：拦截所有调用路径
// 检查 transform name 是否匹配 "SceneObj_DropItem_Monster"，是则跳过
// ============================================================================

static uint8_t* g_base = nullptr;

typedef __int64(__fastcall* tGetName)(__int64 a1, __int64 a2);

static tGetName GetNameFn()
{
    static tGetName fn = nullptr;
    if (!fn)
        fn = (tGetName)(g_base + Offsets::RVA::GetName);
    return fn;
}

// ============================================================================
// Helpers
// ============================================================================
static uint8_t* AllocNear(uint8_t* target, size_t size) {
    DWORD protect = PAGE_READWRITE;
#ifdef HIDE_TRAMPOLINE
    // Allocate as RW first, will change to RX after writing
    protect = PAGE_READWRITE;
#else
    protect = PAGE_EXECUTE_READWRITE;
#endif
    const uint64_t STEP = 0x100000;
    for (int64_t d = 0; d < 0x40000000; d += STEP) {
        uint8_t* r = (uint8_t*)VirtualAlloc(target + d, size, MEM_COMMIT | MEM_RESERVE, protect);
        if (r) return r;
        if (d > 0) {
            if ((uintptr_t)(target - d) > (uintptr_t)target) continue;
            r = (uint8_t*)VirtualAlloc(target - d, size, MEM_COMMIT | MEM_RESERVE, protect);
            if (r) return r;
        }
    }
    uint8_t* r = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, protect);
    LOG("Pillar", "AllocNear forced fallback to %llX", (uint64_t)r);
    return r;
}

static inline void WriteAbsJmp(uint8_t** pp, uint8_t* target) {
    *(*pp)++ = 0x48; *(*pp)++ = 0xB8;
    *(uint64_t*)(*pp) = (uint64_t)target; *pp += 8;
    *(*pp)++ = 0xFF; *(*pp)++ = 0xE0;
}

static inline void WriteRelJmp(uint8_t** pp, uint8_t* target) {
    int64_t d = target - (*pp + 5);
    if (d >= (int64_t)0x7FFFFFFF || d <= (int64_t)-0x7FFFFFFF) {
        WriteAbsJmp(pp, target);
    }
    else {
        *(*pp)++ = 0xE9;
        *(int32_t*)(*pp) = (int32_t)d; *pp += 4;
    }
}

// ============================================================================
// Hook state — B81160 entry hook
// ============================================================================
static constexpr int B81160_STOLEN = 16;  // bytes to steal from entry

static uint8_t* g_b81160_entry   = nullptr;
static uint8_t* g_b81160_tramp   = nullptr;
static uint8_t* g_b81160_origEntry = nullptr;
static uint8_t  g_b81160_orig[16] = {};

// ============================================================================
// Helper: read object name from Il2CppString* return value
// Returns true if name matches, false otherwise
// ============================================================================
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

// ============================================================================
// C handler: called from trampoline
// Arguments are the same as B81160's original args (in rcx, rdx, r8, r9)
// Returns 0 = block, 1 = proceed
// ============================================================================
static __int64 __fastcall B81160_Handler(__int64 a1, unsigned int a2, __int64 a3, __int64 a4) {
    // Delayed init on first call (no CreateThread needed)
    RunDelayedInit();

    auto gn = GetNameFn();
    if (gn && Config::g_pillarFilterEnabled && Config::g_pillarSuppressEnabled) {
        // a3 = transform/object; a2 = typeId
        __int64 np = 0;
        __try {
            np = gn(a3, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 1;
        }
        if (np && IsDropItemMonster(np)) {
            char nameUtf8[256] = {};
            int nameLen = 0;
            __try { nameLen = *(uint16_t*)(np + 0x10); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            if (nameLen > 0) {
                WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)(np + 0x14), nameLen, nameUtf8, 254, nullptr, nullptr);
            }
            LOG("Pillar", "BLOCK: '%s' type=%u", nameUtf8, a2);
            return 0;
        }
    }
    return 1;
}

// ============================================================================
// Init — install B81160 entry hook
// ============================================================================
static bool DoInit()
{
    g_base = (uint8_t*)GetModuleHandleW(L"YuanShen.exe");
    if (!g_base)
        g_base = (uint8_t*)GetModuleHandleW(nullptr);
    if (!g_base) return false;

    g_b81160_entry = g_base + Offsets::RVA::B81160_RVA;
    memcpy(g_b81160_orig, g_b81160_entry, B81160_STOLEN);
    LOG("Pillar", "B81160 entry[0..15]: %02X %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X %02X %02X %02X %02X",
        g_b81160_orig[0], g_b81160_orig[1], g_b81160_orig[2], g_b81160_orig[3],
        g_b81160_orig[4], g_b81160_orig[5], g_b81160_orig[6], g_b81160_orig[7],
        g_b81160_orig[8], g_b81160_orig[9], g_b81160_orig[10], g_b81160_orig[11],
        g_b81160_orig[12], g_b81160_orig[13], g_b81160_orig[14], g_b81160_orig[15]);

    g_b81160_tramp = AllocNear(g_b81160_entry, 4096);
    if (!g_b81160_tramp) { LOG_MSG("Pillar", "B81160 alloc fail"); return false; }

    uint8_t* p = g_b81160_tramp;

    // Section A: original entry (stolen bytes), then jmp back
    g_b81160_origEntry = p;
    memcpy(p, g_b81160_orig, B81160_STOLEN);
    p += B81160_STOLEN;
    WriteRelJmp(&p, g_b81160_entry + B81160_STOLEN);

    // Section B: check entry (the actual hook target)
    uint8_t* checkEntry = p;

    // Save regs
    *p++ = 0x51; *p++ = 0x52;
    *p++ = 0x41; *p++ = 0x50; *p++ = 0x41; *p++ = 0x51;
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xEC; *p++ = 0x28;

    // call B81160_Handler (args already in rcx..r9)
    *p++ = 0x48; *p++ = 0xB8; *(uint64_t*)p = (uint64_t)B81160_Handler; p += 8;
    *p++ = 0xFF; *p++ = 0xD0;

    *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x74; uint8_t* jz = p; p++;  // jz SKIP

    // PROCEED: restore, jmp to original entry
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x28;
    *p++ = 0x41; *p++ = 0x59; *p++ = 0x41; *p++ = 0x58;
    *p++ = 0x5A; *p++ = 0x59;
    WriteRelJmp(&p, g_b81160_origEntry);

    *jz = (uint8_t)(p - (jz + 1));

    // SKIP: restore, ret 0
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x28;
    *p++ = 0x41; *p++ = 0x59; *p++ = 0x41; *p++ = 0x58;
    *p++ = 0x5A; *p++ = 0x59;
    *p++ = 0x33; *p++ = 0xC0;  // xor eax,eax
    *p++ = 0xC3;  // ret

    size_t sz = (size_t)(p - g_b81160_tramp);
    LOG("Pillar", "B81160 tramp at %llX orig=%llX check=%llX sz=%zu",
        (uint64_t)g_b81160_tramp, (uint64_t)g_b81160_origEntry, (uint64_t)checkEntry, sz);

#ifdef HIDE_TRAMPOLINE
    // Hide trampoline: change from RW to RX (no write)
    {
        DWORD oldProt = 0;
        VirtualProtect(g_b81160_tramp, sz, PAGE_EXECUTE_READ, &oldProt);
    }
#endif

    // Write 16-byte jmp at entry (FF 25 + addr + padding)
    uint8_t hook[16] = {};
    hook[0] = 0xFF; hook[1] = 0x25;  // jmp [rip+0]
    hook[2] = 0; hook[3] = 0; hook[4] = 0; hook[5] = 0;
    *(uint64_t*)(hook + 6) = (uint64_t)checkEntry;

    DWORD old = 0;
    VirtualProtect(g_b81160_entry, B81160_STOLEN, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_b81160_entry, hook, B81160_STOLEN);
    VirtualProtect(g_b81160_entry, B81160_STOLEN, old, &old);
    LOG_MSG("Pillar", "B81160 -> tramp OK (16-byte jmp)");
    return true;
}

// ============================================================================
// Interface
// ============================================================================
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
    // Restore B81160 entry
    if (g_b81160_entry && g_b81160_orig[0]) {
        DWORD old = 0;
        VirtualProtect(g_b81160_entry, B81160_STOLEN, PAGE_EXECUTE_READWRITE, &old);
        memcpy(g_b81160_entry, g_b81160_orig, B81160_STOLEN);
        VirtualProtect(g_b81160_entry, B81160_STOLEN, old, &old);
    }

    // Free trampoline
    if (g_b81160_tramp) { VirtualFree(g_b81160_tramp, 0, MEM_RELEASE); g_b81160_tramp = nullptr; }

    g_base = nullptr;
    LOG_MSG("Pillar", "Uninit OK");
}
