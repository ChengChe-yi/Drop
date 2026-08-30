#pragma once

// ============================================================================
// Lists —— 白/黑名单引擎（Whitelist.ini / Blacklist.ini）。
//
// 名单文件分两个区：
//   [Text]  按 text 精确匹配（物品名 / 交互显示文本）
//   [Icon]  按 icon 子串匹配（如 UI_ItemIcon_112 命中整个 112 图标族）
// 判定顺序：黑名单 > 白名单 > 默认（由调用方解释 Default）。
// 双缓冲快照：watcher 线程是唯一写者，读取方 acquire 锁定快照，无锁。
// ============================================================================
namespace Lists
{
    enum class Verdict
    {
        Default,   // 名单未命中，由调用方走默认逻辑
        Allow,     // 白名单命中：放行
        Block,     // 黑名单命中：拦截
    };

    // 读取 Whitelist.ini / Blacklist.ini 并翻转快照（watcher 线程调用）。
    // 文件缺失时保留当前快照。
    void LoadAll();

    // 综合判定。text/icon 任一为空串或 nullptr 时跳过对应维度。
    Verdict Evaluate(const char* text, const char* icon);

    // 只查黑名单（不涉及白名单）——交互分支用：原本放行，黑名单命中才拦。
    bool IsBlacklisted(const char* text, const char* icon);
}
