#pragma once
#include <cstdint>

// 签名库（IDA 已验证，各函数唯一）：
//
//  B81160:       56 57 55 53 48 83 EC 28 4C 89 CF 4C 89 C6 89 D5
//                48 89 CB 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ??
//                80 3D ?? ?? ?? ?? 00 0F 84 ?? ?? ?? ??
//                48 85 F6 0F 84 ?? ?? ?? ?? 48 89 F1 89 EA E8
//
//  PickupData:   41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 68 48 89 D7 49 89 CE 80 3D ?? ?? ?? ?? 00 0F 85
//
//  GetName:      40 53 48 81 EC ?? ?? ?? ?? 48 8B D9 48 85 C9
//                0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ??
//                48 85 C0 0F 84
//
//  BtnDispatch:  41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 68
//                48 89 D3 49 89 CE 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ??
//                48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0 0F 84 ?? ?? ?? ??
//                48 89 C7 48 89 58 10
// 
//  JCJOGCNAJLF:  41 56 56 57 55 53 48 83 EC 40 ?? ?? ?? ?? ?? ?? ?? ?? ??
//                (push r14/rsi/rdi/rbp/rbx; sub rsp,40h; 三条寄存器 mov ——
//                 前 10 字节编码唯一做版本守卫，mov 区存在 8B/89 两种
//                 等价编码做通配；前 16 字节做 trampoline)

namespace Offsets
{
    namespace RVA
    {
        // 硬编码 RVA，游戏更新后需同步；可用 Scanner 签名定位替代。
        inline constexpr uintptr_t PickupDataAdd    = 0xF4944B0;

        inline constexpr uintptr_t BtnDispatch        = 0xF496C80;

        // 按钮对象接口类指针槽 qword_1455C8B38（InteeBtn 查表用），
        // 运行期由 il2cpp 填充。
        inline constexpr uintptr_t InteeBtnIfaceKlass = 0x55C8B38;

    }
}
