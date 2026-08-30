#pragma once

// 拾取条目过滤 hook（NOEFNCCMEBC）：命中目标图标族且不在白名单的
// 拾取条目不入面板（提示框不再弹出），其余放行并记录。
// 开关见 Config → [PickupFilter]，日志输出依赖 [Log]。
namespace PickupFilter
{
    bool Init();
    void Uninit();
}
