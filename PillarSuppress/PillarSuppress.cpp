#include "pch.h"
#include "PillarSuppress.h"
#include "Config.h"
#include "Patterns.h"
#include "Scanner.h"
#include <MinHook.h>
#include <stdlib.h>
#include <time.h>


#ifdef _DEBUG
#define LOG(fmt, ...) do {                          \
    char _b_[512];                                  \
    sprintf_s(_b_, "[Drop] " fmt "\n", __VA_ARGS__);\
    OutputDebugStringA(_b_);                        \
} while(0)
#else
#define LOG(fmt, ...)
#endif

static uintptr_t g_base = 0;
static bool g_hookActive = false;

static void EnsureBase()
{
    if (g_base) return;
    HMODULE m = GetModuleHandleW(L"YuanShen.exe");
    g_base = m ? (uintptr_t)m : 0;
}

typedef __int64(__fastcall* GetNameFunc)(__int64, int);

static GetNameFunc   o_getName = nullptr;
static void(__fastcall* o_1C40430)(__int64 a1, unsigned int a2) = nullptr;


static void __fastcall Sub1C40430Hook(__int64 a1, unsigned int a2)
{
    __try {
        if (Config::g_pillarFilterEnabled && a1 && o_getName && g_hookActive)
        {
            wchar_t nameBuf[64] = {0};
            __try {
                __int64 go = *(__int64*)(a1 + 0x1E0);
                if (go) {
                    __int64 ns = o_getName(go, 0);
                    if (ns) {
                        int len = *(uint16_t*)(ns + 0x10);
                        if (len > 0 && len < 64) {
                            memcpy(nameBuf, (void*)(ns + 0x14), len * sizeof(wchar_t));
                            nameBuf[len] = 0;
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}

            if (nameBuf[0] && wcsstr(nameBuf, L"SceneObj_DropItem_Monster") == nameBuf)
            {
                return;  
            }
        }
        o_1C40430(a1, a2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
       
    }
}

static bool DoInit()
{
    Offsets::InitOffsets();
    EnsureBase();
    if (!g_base) { return false; }

    srand((unsigned int)time(NULL) ^ (unsigned int)GetTickCount64());
    int delay = (rand() % 26) + 5;
    Sleep(delay * 1000);

    Config::StartHotReload();

    o_getName = (GetNameFunc)(g_base + std::stoull(Offsets::GetNameOffset, nullptr, 16));

    void* c40430 = (void*)(g_base + 0x11C40430);
    MH_STATUS s = MH_CreateHook(c40430, &Sub1C40430Hook, (void**)&o_1C40430);
    if (s != MH_OK) return false;
    MH_EnableHook(c40430);

    g_hookActive = true;

    return true;
}

bool PillarSuppress::Init()
{
    __try { return DoInit(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void PillarSuppress::Uninit()
{
    Config::StopHotReload();
    g_hookActive = false;
    o_getName = nullptr;
}
