#pragma once
#include <cstdint>

namespace Hooks
{
    // 初始化 InteeBtn 基址解析、PickupFilter 与 InteeProbe 两个特性 hook，
    // 并汇总记录各自结果（具体失败原因见各模块日志）。
    // 返回 true 表示至少一个特性 hook 生效（可能部分降级），
    // false 表示全部失败（如游戏版本不匹配导致 prologue mismatch）。
    bool Init();

    // 摘除两个 hook 并调用 MH_Uninitialize；对未初始化的部分失败路径幂等。
    void Uninit();
}
