#include "pch.h"
#include "PickupSuppress.h"
#include "Patterns.h"
#include "Config.h"
#include "Logger.h"
#include "XorStr.h"
#include <cstring>

// ============================================================================
// PickupSuppress — 拾取提示框屏蔽
// Hook PickupData (EFNHHIFBIMP) 入口
// 检查 icon 字段是否以 UI_ItemIcon_112 开头，是则拦截
// ============================================================================

static uint8_t* g_base = nullptr;

// IL2CPP string (shared definition, matches PillarSuppress)
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
// Helpers
// ============================================================================
static void EnsureBase()
{
    if (g_base) return;
    g_base = (uint8_t*)GetModuleHandleW(L"YuanShen.exe");
    if (!g_base)
        g_base = (uint8_t*)GetModuleHandleW(nullptr);
}

static uint8_t* AllocNear(uint8_t* target, size_t size) {
#ifdef HIDE_TRAMPOLINE
    DWORD prot = PAGE_READWRITE;
#else
    DWORD prot = PAGE_EXECUTE_READWRITE;
#endif
    const uint64_t STEP = 0x100000;
    for (int64_t d = 0; d < 0x40000000; d += STEP) {
        uint8_t* r = (uint8_t*)VirtualAlloc(target + d, size, MEM_COMMIT | MEM_RESERVE, prot);
        if (r) return r;
        if (d > 0) {
            if ((uintptr_t)(target - d) > (uintptr_t)target) continue;
            r = (uint8_t*)VirtualAlloc(target - d, size, MEM_COMMIT | MEM_RESERVE, prot);
            if (r) return r;
        }
    }
    uint8_t* r = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, prot);
    LOG("PDhook", "AllocNear forced fallback to %llX", (uint64_t)r);
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
// Hook state
// ============================================================================
static constexpr int PD_STOLEN = 16;

static uint8_t* g_pd_entry = nullptr;
static uint8_t* g_pd_tramp = nullptr;
static uint8_t* g_pd_origEntry = nullptr;
static uint8_t  g_pd_orig[16] = {};

// ============================================================================
// C handler: returns 0 = block, 1 = proceed
// ============================================================================
static __int64 __fastcall PD_Handler(__int64 a1, __int64 a2) {
    // Lazy config hot-reload poll (runs on the game thread)
    Config::Tick();

    char iconUtf8[64] = {}, nameUtf8[64] = {};
    __try {
        if (a2 && Config::g_pillarFilterEnabled && Config::g_pickupSuppressEnabled) {
            // Read icon (offset 0x30)
            Il2CppString* icon = *(Il2CppString**)(a2 + 0x30);
            if (icon && icon->chars && icon->length > 0 && icon->length < 100) {
                int cl = (icon->length < 30) ? icon->length : 30;
                WideCharToMultiByte(CP_UTF8, 0, icon->chars, cl, iconUtf8, 62, nullptr, nullptr);
            }

            // Read name (offset 0x20 — nested try/except)
            __try {
                Il2CppString* nm = *(Il2CppString**)(a2 + 0x20);
                if (nm && nm->chars && nm->length > 0 && nm->length < 50) {
                    int cl = (nm->length < 30) ? nm->length : 30;
                    WideCharToMultiByte(CP_UTF8, 0, nm->chars, cl, nameUtf8, 62, nullptr, nullptr);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}

            if (XSTRSTR(iconUtf8, "UI_ItemIcon_112")) {
                LOG("PDhook", "BLOCK icon='%s' name='%s'", iconUtf8, nameUtf8);
                return 0;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 1;
}

// ============================================================================
// Init
// ============================================================================
static bool DoInit()
{
    EnsureBase();
    if (!g_base) return false;

    g_pd_entry = g_base + Offsets::RVA::PickupDataAdd;
    memcpy(g_pd_orig, g_pd_entry, PD_STOLEN);
    LOG("PDhook", "entry[0..15]: %02X %02X %02X %02X %02X %02X %02X %02X "
                  "%02X %02X %02X %02X %02X %02X %02X %02X",
        g_pd_orig[0], g_pd_orig[1], g_pd_orig[2], g_pd_orig[3],
        g_pd_orig[4], g_pd_orig[5], g_pd_orig[6], g_pd_orig[7],
        g_pd_orig[8], g_pd_orig[9], g_pd_orig[10], g_pd_orig[11],
        g_pd_orig[12], g_pd_orig[13], g_pd_orig[14], g_pd_orig[15]);

    g_pd_tramp = AllocNear(g_pd_entry, 4096);
    if (!g_pd_tramp) { LOG_MSG("PDhook", "alloc fail"); return false; }

    uint8_t* p = g_pd_tramp;

    // Section A: original entry (stolen bytes)
    g_pd_origEntry = p;
    memcpy(p, g_pd_orig, PD_STOLEN);
    p += PD_STOLEN;
    WriteRelJmp(&p, g_pd_entry + PD_STOLEN);

    // Section B: check entry
    uint8_t* checkEntry = p;

    // Save regs
    *p++ = 0x51; *p++ = 0x52;
    *p++ = 0x41; *p++ = 0x50; *p++ = 0x41; *p++ = 0x51;
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xEC; *p++ = 0x28;

    // call PD_Handler
    *p++ = 0x48; *p++ = 0xB8; *(uint64_t*)p = (uint64_t)PD_Handler; p += 8;
    *p++ = 0xFF; *p++ = 0xD0;

    *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x74; uint8_t* jz = p; p++;

    // PROCEED: restore, jmp to original entry
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x28;
    *p++ = 0x41; *p++ = 0x59; *p++ = 0x41; *p++ = 0x58;
    *p++ = 0x5A; *p++ = 0x59;
    WriteRelJmp(&p, g_pd_origEntry);

    *jz = (uint8_t)(p - (jz + 1));

    // SKIP: restore, ret 0
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x28;
    *p++ = 0x41; *p++ = 0x59; *p++ = 0x41; *p++ = 0x58;
    *p++ = 0x5A; *p++ = 0x59;
    *p++ = 0x33; *p++ = 0xC0;
    *p++ = 0xC3;

    size_t sz = (size_t)(p - g_pd_tramp);
    LOG("PDhook", "tramp at %llX orig=%llX check=%llX sz=%zu",
        (uint64_t)g_pd_tramp, (uint64_t)g_pd_origEntry, (uint64_t)checkEntry, sz);

#ifdef HIDE_TRAMPOLINE
    // Hide trampoline: change from RW to RX
    {
        DWORD oldProt = 0;
        VirtualProtect(g_pd_tramp, sz, PAGE_EXECUTE_READ, &oldProt);
    }
#endif

    // Write 16-byte jmp at entry
    uint8_t hook[16] = {};
    hook[0] = 0xFF; hook[1] = 0x25;
    hook[2] = 0; hook[3] = 0; hook[4] = 0; hook[5] = 0;
    *(uint64_t*)(hook + 6) = (uint64_t)checkEntry;

    DWORD old = 0;
    VirtualProtect(g_pd_entry, PD_STOLEN, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_pd_entry, hook, PD_STOLEN);
    VirtualProtect(g_pd_entry, PD_STOLEN, old, &old);
    LOG_MSG("PDhook", "PickupData -> tramp OK");
    return true;
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
    // Restore original bytes
    if (g_pd_entry && g_pd_orig[0]) {
        DWORD old = 0;
        VirtualProtect(g_pd_entry, PD_STOLEN, PAGE_EXECUTE_READWRITE, &old);
        memcpy(g_pd_entry, g_pd_orig, PD_STOLEN);
        VirtualProtect(g_pd_entry, PD_STOLEN, old, &old);
    }

    // Free trampoline
    if (g_pd_tramp) { VirtualFree(g_pd_tramp, 0, MEM_RELEASE); g_pd_tramp = nullptr; }

    g_pd_entry = nullptr;
    g_pd_origEntry = nullptr;

    LOG_MSG("PDhook", "Uninit OK");
}
