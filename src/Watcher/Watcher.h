#pragma once

// ============================================================================
// Watcher —— 事件驱动热重载。
//
// 单线程 overlapped ReadDirectoryChangesW 监听 DLL 所在目录，
// 命中 Config.ini / Whitelist.ini 后防抖 200ms，再对比 mtime，
// 只对真正变化的文件触发回调（在 watcher 线程执行）。
// 目录句柄不可用或 RDCW 失败时自动降级为 1s 轮询。
// 回调禁止触碰 loader / LoadLibrary（当前两个回调均为纯读 + 原子写）。
// ============================================================================

namespace Watcher
{
    // dir：待监听目录（宽字符，含尾部反斜杠）。线程创建失败返回 false。
    // 依次监听 Config.ini / Whitelist.ini / Blacklist.ini。
    bool Start(const wchar_t* dir,
               void (*onConfigChange)(),
               void (*onWhitelistChange)(),
               void (*onBlacklistChange)());

    // 通知线程退出并有界等待；Start 失败后调用也安全。
    void Stop();
}
