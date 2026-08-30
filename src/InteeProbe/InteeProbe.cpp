#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "InteeProbe.h"
#include <MinHook.h>
#include "InteeBtn.h"
#include "Patterns.h"
#include "Config.h"
#include "Logger.h"
#include <atomic>
#include <cstdint>
#include <cstring>


static uint8_t* g_base = nullptr;
static void*    g_target = nullptr;
static std::atomic<bool> g_hooked{ false };

typedef __int64 (__fastcall* tOriginal2)(__int64, __int64);
static tOriginal2 g_orig = nullptr;


static bool ProbeActive()
{
    return Config::g_inteeProbeEnabled.load(std::memory_order_acquire) &&
           Logger::g_logWriteEnabled.load(std::memory_order_acquire);
}


static void AppendField(char* buf, int bufChars, int* used,
                        const char* key, const char* value)
{
    int n = sprintf_s(buf + *used, (size_t)(bufChars - *used), "%s='%s'", key, value);
    if (n > 0) *used += n;
}

static void AppendRaw(char* buf, int bufChars, int* used, const char* text)
{
    int n = sprintf_s(buf + *used, (size_t)(bufChars - *used), "%s", text);
    if (n > 0) *used += n;
}



static void PadBytes(char* buf, int bufChars, int* used, int width)
{
    while (*used < width && *used < bufChars - 1)
        buf[(*used)++] = ' ';
}

static void LogBtnFields(__int64 a2)
{
    char nameUtf8[96] = {};
    char dispUtf8[96] = {};
    char iconUtf8[96] = {};

    __try {
        // 文本走字段（变体布局：+0x18 物品名 / +0x20 交互文本）。
        InteeBtn::ReadIl2CppUtf8(*(__int64*)(a2 + 0x18), nameUtf8, 96);
        InteeBtn::ReadIl2CppUtf8(*(__int64*)(a2 + 0x20), dispUtf8, 96);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    // 图标统一走接口方法 #2（查表，含 NPC 对话的配置回退）。
    InteeBtn::ReadStringUtf8((void*)a2, 2, iconUtf8, 96);

    char line[512] = {};
    int used = 0;
    AppendField(line, sizeof(line), &used, "icon", iconUtf8);
    PadBytes(line, sizeof(line), &used, 34);
    AppendRaw(line, sizeof(line), &used, " ");
    if (nameUtf8[0] && dispUtf8[0]) {
        AppendField(line, sizeof(line), &used, "name", nameUtf8);
        AppendRaw(line, sizeof(line), &used, " ");
        AppendField(line, sizeof(line), &used, "disp", dispUtf8);
    } else {
        AppendField(line, sizeof(line), &used, "text",
                    nameUtf8[0] ? nameUtf8 : dispUtf8);
    }

    LOG("交互类", "%s", line);
}

static __int64 __fastcall ProbeHandler(__int64 a1, __int64 a2)
{
    if (a2 && ProbeActive())
        LogBtnFields(a2);
    return g_orig(a1, a2);
}

static bool DoInit()
{
    if (!g_base)
        g_base = (uint8_t*)GetModuleHandleW(nullptr);
    if (!g_base) return false;

    g_target = g_base + Offsets::RVA::BtnDispatch;

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        LOG("交互类", "MH_Initialize failed: %s", MH_StatusToString(st));
        return false;
    }

    st = MH_CreateHook(g_target, &ProbeHandler,
                       reinterpret_cast<void**>(&g_orig));
    if (st != MH_OK) {
        LOG("交互类", "MH_CreateHook failed: %s", MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(g_target);
    if (st != MH_OK) {
        LOG("交互类", "MH_EnableHook failed: %s", MH_StatusToString(st));
        return false;
    }

    g_hooked.store(true, std::memory_order_release);
    LOG("交互类", "target=%llX (MinHook)", (uint64_t)g_target);
    return true;
}

bool InteeProbe::Init()
{
    __try { return DoInit(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_MSG("交互类", "Init EXCEPTION!");
        return false;
    }
}

void InteeProbe::Uninit()
{
    if (!g_hooked.exchange(false))
        return;

    MH_DisableHook(g_target);
    MH_RemoveHook(g_target);
    g_orig = nullptr;
    LOG_MSG("交互类", "Uninit OK");
}
