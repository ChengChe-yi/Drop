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

// ============================================================================
// Trampoline 常驻 hook —— 初始化时写一次游戏内存，热路径不再触碰。
//
//   target:  [16 字节 prologue][函数其余部分...]
//            被一次性覆盖为: FF 25 xx | PD_Handler（14 字节绝对 jmp）+ 90 90
//            NOP 填充让 patch 区以指令边界结束（sub rsp,68h 保持完整）
//
//   trampoline: [16 字节 prologue 原样拷贝] + FF 25 xx | target+16
//
//   prologue @ RVA 0xF4944B0（IDA 已验证）:
//     41 57 41 56 41 55 41 54   push r15 / r14 / r13 / r12
//     56 57 55 53               push rsi / rdi / rbp / rbx
//     48 83 EC 68               sub rsp, 68h    ← 止于 +0x10
//   拷贝区间全为位置无关指令（无 RIP 相对寻址），原样拷贝零重定位；
//   trampoline 重执行完整 prologue 后跳回 target+16，栈状态与原函数一致。
// ============================================================================

static constexpr size_t kPatchLen = 16;

// 版本守卫基准：字节不匹配（游戏已更新）则拒绝 hook。
static const uint8_t kExpectedPrologue[kPatchLen] = {
    0x41, 0x57,         // push r15
    0x41, 0x56,         // push r14
    0x41, 0x55,         // push r13
    0x41, 0x54,         // push r12
    0x56, 0x57, 0x55, 0x53, // push rsi, rdi, rbp, rbx
    0x48, 0x83, 0xEC, 0x68  // sub rsp, 68h
};

static uint8_t* g_base = nullptr;
static uint8_t* g_target = nullptr;
static uint8_t  g_origBytes[kPatchLen] = {};
static uint8_t* g_trampoline = nullptr;
static std::atomic<bool> g_hooked{ false };

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

// SEH 保护下的 IL2CppString → UTF-8 读取（指针有效性未知，随时可能异常）。
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
    return ((tOriginal)g_trampoline)(a1, a2);
}

static __int64 __fastcall PD_Handler(__int64 a1, __int64 a2)
{
    // 热路径只做内存操作；文件 IO 全部归 watcher 线程。

    if (!a2)
        return CallOriginal(a1, a2);

    const bool suppress = Config::g_pickupSuppressEnabled.load(std::memory_order_acquire);
    const bool logging  = Logger::g_logWriteEnabled.load(std::memory_order_acquire);

    // 两个开关全关时直接放行——每个拾取都会经过这里，早退是最大收益。
    if (!suppress && !logging)
        return CallOriginal(a1, a2);

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

    // 进入函数的每个拾取都记录 icon 和 name；BLOCK 仅用于命中的目标图标。
    if (logging) {
        LOG("PDhook", "%s icon='%s' name='%s'",
            shouldBlock ? "BLOCK" : "PASS", iconUtf8, nameUtf8);
    }

    if (shouldBlock)
        return 0;

    return CallOriginal(a1, a2);
}

static bool DoInit()
{
    if (!g_base)
        g_base = (uint8_t*)GetModuleHandleW(nullptr);
    if (!g_base) return false;

    g_target = g_base + Offsets::RVA::PickupDataAdd;

    memcpy(g_origBytes, g_target, kPatchLen);
    if (memcmp(g_origBytes, kExpectedPrologue, kPatchLen) != 0) {
        LOG("PDhook", "prologue mismatch at %llX — game updated? hook refused",
            (uint64_t)g_target);
        return false;
    }

    // 1) 建 trampoline：RW 写入后翻 RX（W^X）。
    g_trampoline = (uint8_t*)VirtualAlloc(nullptr, kPatchLen + 14,
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
    if (!g_trampoline) return false;

    memcpy(g_trampoline, g_origBytes, kPatchLen);
    BuildJmpBytes(g_trampoline + kPatchLen, g_target + kPatchLen);

    DWORD old = 0;
    if (!VirtualProtect(g_trampoline, kPatchLen + 14, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
        return false;
    }

    // 2) 一次性 patch target：绝对 jmp + 2 NOP 补齐指令边界。
    uint8_t patch[kPatchLen];
    BuildJmpBytes(patch, PD_Handler);
    patch[14] = 0x90;
    patch[15] = 0x90;

    if (!VirtualProtect(g_target, kPatchLen, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
        return false;
    }
    memcpy(g_target, patch, kPatchLen);
    VirtualProtect(g_target, kPatchLen, old, &old);

    g_hooked.store(true, std::memory_order_release);

    LOG("PDhook", "target=%llX trampoline=%llX",
        (uint64_t)g_target, (uint64_t)g_trampoline);
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

// 仅 worker 收尾路径调用一次（模块已 PIN，当前无触发方）；热路径永不触及。
// 若将来把 g_stop 用作运行时停用：在途线程可能仍在 trampoline 内，
// 此处的释放会带竞态，届时应保留页面不释放。
void PickupSuppress::Uninit()
{
    if (!g_hooked.exchange(false))
        return;

    if (g_target) {
        DWORD old = 0;
        if (VirtualProtect(g_target, kPatchLen, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(g_target, g_origBytes, kPatchLen);
            VirtualProtect(g_target, kPatchLen, old, &old);
        }
        LOG_MSG("PDhook", "Uninit OK (prologue restored)");
    }

    if (g_trampoline) {
        DWORD old = 0;
        VirtualProtect(g_trampoline, kPatchLen + 14, PAGE_READWRITE, &old);
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
    }
}
