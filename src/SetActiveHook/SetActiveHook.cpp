#include "pch.h"
#include "SetActiveHook.h"
#include "PickupSuppress.h"
#include "PillarSuppress.h"
#include "Patterns.h"
#include "Logger.h"
#include "XorStr.h"
#include <atomic>

struct DiagString {
    void* klass;
    void* monitor;
    int32_t length;
    wchar_t chars[1];
};

typedef DiagString* (*tGetName)(void*);

static tGetName          s_getName = nullptr;
static uint8_t*          g_saTarget = nullptr;
static uint8_t           g_saOrig[14] = {};
static uint8_t           g_saJmp[14] = {};
static bool              g_saHooked = false;
static std::atomic<bool> g_inGame{false};
static bool              g_prevLoginActive = false;

static void BuildJmp14(uint8_t* dest, void* target)
{
    dest[0] = 0xFF; dest[1] = 0x25;
    dest[2] = 0; dest[3] = 0; dest[4] = 0; dest[5] = 0;
    *(void**)(dest + 6) = target;
}

void SetActiveHook::Cancel()
{
    g_inGame = false;
}

static void OnLoginMainPage(char active)
{
    if (active && !g_prevLoginActive) {
        g_prevLoginActive = true;
        if (g_inGame.exchange(false)) {
            PickupSuppress::Uninit();
            PillarSuppress::Uninit();
            LOG_MSG("SAdiag", "<- LoginMainPage(1) uninstalled hooks");
        }
    } else if (!active && g_prevLoginActive) {
        g_prevLoginActive = false;
        g_inGame = true;
        LOG_MSG("SAdiag", "-> LoginMainPage(0) install hooks in 3s");
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(3000);
            if (g_inGame) {
                PickupSuppress::Init();
                PillarSuppress::Init();
                LOG_MSG("SAdiag", "-> Hooks installed (3s delay)");
            }
            return 0;
        }, nullptr, 0, nullptr);
    }
}

static void __fastcall SA_Handler(__int64 proxy, __int64 value)
{
    __try {
        DiagString* str = s_getName((void*)proxy);
        if (str && str->length == 13) {
            wchar_t target[] = L"LoginMainPage";
            bool match = true;
            for (int i = 0; i < 13; i++) {
                if (str->chars[i] != target[i]) { match = false; break; }
            }
            if (match) OnLoginMainPage((char)value);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    DWORD old = 0;
    VirtualProtect(g_saTarget, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_saTarget, g_saOrig, 14);
    VirtualProtect(g_saTarget, 14, old, &old);
    ((void(__fastcall*)(__int64, __int64))g_saTarget)(proxy, value);
    VirtualProtect(g_saTarget, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_saTarget, g_saJmp, 14);
    VirtualProtect(g_saTarget, 14, old, &old);
}

bool SetActiveHook::Init()
{
    uintptr_t base = (uintptr_t)GetModuleHandleW(XWSTR(L"YuanShen.exe"));
    if (!base) return false;

    g_saTarget = (uint8_t*)(base + Offsets::RVA::SetActive);
    s_getName = (tGetName)(base + Offsets::RVA::GetName);

    memcpy(g_saOrig, g_saTarget, 14);
    BuildJmp14(g_saJmp, SA_Handler);

    LOG("SAdiag", "SetActive_Wrapper at %llX, first bytes %02X %02X %02X %02X",
        (uint64_t)g_saTarget, g_saOrig[0], g_saOrig[1], g_saOrig[2], g_saOrig[3]);

    DWORD old = 0;
    VirtualProtect(g_saTarget, 14, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_saTarget, g_saJmp, 14);
    VirtualProtect(g_saTarget, 14, old, &old);

    g_saHooked = true;
    LOG_MSG("SAdiag", "Hook installed OK");
    return true;
}

void SetActiveHook::Uninit()
{
    if (g_saHooked && g_saTarget) {
        DWORD old = 0;
        VirtualProtect(g_saTarget, 14, PAGE_EXECUTE_READWRITE, &old);
        memcpy(g_saTarget, g_saOrig, 14);
        VirtualProtect(g_saTarget, 14, old, &old);
        g_saHooked = false;
    }
}
