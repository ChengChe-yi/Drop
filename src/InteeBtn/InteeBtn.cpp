#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "InteeBtn.h"
#include "Patterns.h"
#include <cstring>

// ============================================================================
// 交互按钮对象统一读取。
//
// 按钮对象(AHJCGJKAGFD)是混淆加固类型：dump.cs 无字段偏移与接口声明，
// 方法调用走对象内嵌的手工接口表。查表与调用复刻游戏反汇编
//（loc_14F496D1B）：
//   klass    = [obj]
//   接口表   @ klass+0x10，每项 16 字节 {接口类指针, 槽位基址}
//   接口数   u16 @ klass+0xC2；方法数 u16 @ klass+0xBE
//   fn       = [klass + 8*(off + slot) + 0xD0]
//   method   = [klass + 8*(off + mcount + slot) + 0xD0]
//   调用约定 fn(obj, method)（mov rcx,rsi / call rbp）
// 解析出的均为 getter（游戏原函数体内也反复调用同批方法），无副作用。
// ============================================================================

static void** g_ifaceKlassSlot = nullptr;   // qword_1455C8B38：接口类指针槽，运行期由 il2cpp 填充

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

// SEH 保护的 Il2CppString → UTF-8 读取（指针有效性未知，随时可能异常）。
static bool ReadIl2CppUtf8SEH(__int64 p, char* out, int outSize)
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

void InteeBtn::Init()
{
    uint8_t* base = (uint8_t*)GetModuleHandleW(nullptr);
    if (!base) return;
    g_ifaceKlassSlot = (void**)(base + Offsets::RVA::InteeBtnIfaceKlass);
}

bool InteeBtn::CallGetter(void* btn, int slot, __int64* out)
{
    *out = 0;
    if (!btn || slot < 0 || !g_ifaceKlassSlot) return false;
    void* ifaceKlass = *g_ifaceKlassSlot;
    if (!ifaceKlass) return false;

    __try {
        uint8_t* klass = *(uint8_t**)btn;
        if (!klass) return false;
        uint32_t ifaceCnt = *(uint16_t*)(klass + 0xC2);
        uint32_t mcount = *(uint16_t*)(klass + 0xBE);
        uint8_t* map = *(uint8_t**)(klass + 0x10);
        if (!map || ifaceCnt == 0) return false;

        for (uint32_t i = 0; i < ifaceCnt * 16u; i += 16) {
            if (*(void**)(map + i) != ifaceKlass) continue;
            uint32_t idx = *(uint32_t*)(map + i + 8) + (uint32_t)slot;
            void* fn = *(void**)(klass + 8ull * idx + 0xD0);
            void* method = *(void**)(klass + 8ull * (idx + mcount) + 0xD0);
            if (!fn) return false;
            *out = ((__int64 (__fastcall*)(void*, void*))fn)(btn, method);
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InteeBtn::ReadStringUtf8(void* btn, int slot, char* out, int outSize)
{
    __int64 p = 0;
    if (!CallGetter(btn, slot, &p)) return false;
    return ReadIl2CppUtf8SEH(p, out, outSize);
}

bool InteeBtn::ReadIl2CppUtf8(__int64 str, char* out, int outSize)
{
    return ReadIl2CppUtf8SEH(str, out, outSize);
}
