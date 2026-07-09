#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────
//  偏移配置 — 单独文件方便维护
//  格式: 基址 + 偏移 = IDA 地址
//  当游戏更新时只需改这里
// ─────────────────────────────────────────────────────
namespace Offsets
{
    // 物品光柱创建函数入口
    inline constexpr uintptr_t kSub151C40880 = 0x11C40880;

    // 物品光柱具体创建 (内部调用)
    inline constexpr uintptr_t kSub151B81160 = 0x11B81160;

    // Unity GameObject.GetName
    inline constexpr uintptr_t kGetName      = 0x1117FF0;

    // 拾取提示框创建 (在 B81160 内部调用)
    inline constexpr uintptr_t kSub154DFF010 = 0x14DFF010;

    // a1 结构体中物品类型的字节偏移
    inline constexpr uint32_t  kTypeFieldOffset = 0x3D8;

    // a1 结构体中 GameObject 指针的数组下标 (a1[60])
    inline constexpr int       kObjFieldIndex   = 60;

    // 圣遗物类型值
    inline constexpr int       kRelicTypeValue  = 17;

    // 后续可加 per-version 命名空间:
    // namespace CN { ... }
    // namespace OS { ... }
    // namespace Global { ... }
}
