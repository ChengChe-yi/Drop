#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>

namespace Config
{
    // 拾取过滤开关 (Config.ini → [PickupFilter])
    extern std::atomic<bool> g_pickupFilterEnabled;

    // 白名单启用 (Config.ini → [Whitelist])
    extern std::atomic<bool> g_whitelistEnabled;

    // 黑名单启用 (Config.ini → [Blacklist])
    extern std::atomic<bool> g_blacklistEnabled;

    // 日志开关 (Config.ini → [Log])
    extern std::atomic<bool> g_logEnabled;

    void Reload();

    // 返回 Config.ini 完整路径（DLL 同目录），失败返回 0。
    size_t GetConfigPath(wchar_t* buf, size_t bufChars);

    // 返回本 DLL 所在目录（含尾部反斜杠），结果缓存。
    size_t GetModuleDir(wchar_t* buf, size_t bufChars);

    // 启动事件驱动热重载（Config.ini → Reload，Whitelist.ini → Lists::LoadWhitelist，
    // Blacklist.ini → Lists::LoadBlacklist；初始加载与回调共用同一入口）
    void StartHotReload();
    void StopHotReload();
}
