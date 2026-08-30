#pragma once
#include <cstdint>

// 交互按钮对象（AHJCGJKAGFD / InteractionInteeBtn）统一读取工具：
// 接口方法查表调用 + Il2CppString → UTF-8。
// 供 PickupSuppress / InteeProbe 两个 hook 共用，替代各自的字段直读。
namespace InteeBtn
{
    // 解析模块基址、缓存接口类指针槽地址。Hooks::Init 最先调用。
    void Init();

    // 调用按钮对象接口方法 #slot（getter，查表复刻反汇编 loc_14F496D1B）。
    // *out = rax（int 值或指针，由槽位语义决定）。失败/异常返回 false。
    // 已知槽位：#2 图标路径(String)、#4 键低位、#6 类别枚举、
    //          #14 按钮类型、#16 数量。
    bool CallGetter(void* btn, int slot, __int64* out);

    // 调用方法 #slot 并把返回的 Il2CppString* 读成 UTF-8（如 #2 图标路径）。
    bool ReadStringUtf8(void* btn, int slot, char* out, int outSize);

    // 把任意 Il2CppString* 读成 UTF-8（SEH 保护，指针无效返回 false）。
    bool ReadIl2CppUtf8(__int64 str, char* out, int outSize);
}
