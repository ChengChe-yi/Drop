#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "PickupSuppress.h"
#include <MinHook.h>
#include "Patterns.h"
#include "Config.h"
#include "Logger.h"
#include "Whitelist.h"
#include <atomic>
#include <cstdint>
#include <cstring>

static const uint8_t kExpectedPrologue[16] = {
    0x41, 0x57,                     // push r15
    0x41, 0x56,                     // push r14
    0x41, 0x55,                     // push r13
    0x41, 0x54,                     // push r12
    0x56, 0x57, 0x55, 0x53,         // push rsi, rdi, rbp, rbx
    0x48, 0x83, 0xEC, 0x68          // sub rsp, 68h
};

static uint8_t* g_base = nullptr;
static void*    g_target = nullptr;
static std::atomic<bool> g_hooked{ false };

typedef __int64 (__fastcall* tOriginal2)(__int64, __int64);
static tOriginal2 g_orig = nullptr;

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
        int written = WideCharToMultiByte(CP_UTF8, 0, str->chars, cl,
                                          out, outSize - 1, nullptr, nullptr);
        if (written <= 0) {
            out[0] = 0;   // 失败调用可能已留下部分字节；归空，按读取失败处理。
            return false;
        }
        out[written] = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

static __int64 __fastcall PD_Handler(__int64 a1, __int64 a2)
{
    // 热路径只做内存操作；文件 IO 全部归 watcher 线程。

    if (!a2)
        return g_orig(a1, a2);

    const bool suppress = Config::g_pickupSuppressEnabled.load(std::memory_order_acquire);
    const bool logging  = Logger::g_logWriteEnabled.load(std::memory_order_acquire);

    // 两个开关全关时直接放行——每个拾取都会经过这里，早退是最大收益。
    if (!suppress && !logging)
        return g_orig(a1, a2);

    char iconUtf8[64] = {};
    char nameUtf8[128] = {};
    bool shouldBlock = false;

    __try {
        ReadStringUtf8SEH(*(__int64*)(a2 + 0x28), iconUtf8, 64);

        const bool isTargetIcon =
            iconUtf8[0] && strstr(iconUtf8, "UI_ItemIcon_112") != nullptr;

        // 拦截判定与日志都可能需要 name；仅日志关闭且非目标图标时可省去转换。
        if (logging || isTargetIcon)
            ReadStringUtf8SEH(*(__int64*)(a2 + 0x18), nameUtf8, 128);

        if (suppress && isTargetIcon)
            shouldBlock = !Whitelist::IsPickupAllowed(nameUtf8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // 热路径异常保持静默。
    }

    if (logging)
    {
        char line[320] = {};
        int used = 0;

        AppendField(line, sizeof(line), &used, "icon", iconUtf8);
        PadBytes(line, sizeof(line), &used, 34);
        AppendRaw(line, sizeof(line), &used, " ");
        AppendField(line, sizeof(line), &used, "text", nameUtf8);
        if (shouldBlock)
            AppendRaw(line, sizeof(line), &used, " BLOCK");
        LOG("拾取类", "%s", line);
    }

    if (shouldBlock)
        return 0;

    return g_orig(a1, a2);
}

static bool DoInit()
{
    if (!g_base)
        g_base = (uint8_t*)GetModuleHandleW(nullptr);
    if (!g_base) return false;

    g_target = g_base + Offsets::RVA::PickupDataAdd;

    // 版本守卫：prologue 字节不匹配（游戏已更新）则拒绝 hook。
    uint8_t actual[sizeof(kExpectedPrologue)] = {};
    memcpy(actual, g_target, sizeof(actual));
    if (memcmp(actual, kExpectedPrologue, sizeof(actual)) != 0) {
        LOG("拾取类", "prologue mismatch at %llX — game updated?",
            (uint64_t)g_target);
        return false;
    }

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        LOG("拾取类", "MH_Initialize failed: %s", MH_StatusToString(st));
        return false;
    }

    st = MH_CreateHook(g_target, &PD_Handler,
                       reinterpret_cast<void**>(&g_orig));
    if (st != MH_OK) {
        LOG("拾取类", "MH_CreateHook failed: %s", MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(g_target);
    if (st != MH_OK) {
        LOG("拾取类", "MH_EnableHook failed: %s", MH_StatusToString(st));
        return false;
    }

    g_hooked.store(true, std::memory_order_release);
    LOG("拾取类", "target=%llX (MinHook)", (uint64_t)g_target);
    return true;
}

bool PickupSuppress::Init()
{
    __try { return DoInit(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG_MSG("拾取类", "Init EXCEPTION!");
        return false;
    }
}

void PickupSuppress::Uninit()
{
    if (!g_hooked.exchange(false))
        return;

    MH_DisableHook(g_target);
    MH_RemoveHook(g_target);
    g_orig = nullptr;
    LOG_MSG("拾取类", "Uninit OK");
}
