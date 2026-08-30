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
    size_t GetConfigPath(char* buf, size_t bufChars);

    // 返回本 DLL 所在目录（含尾部反斜杠），结果缓存。
    size_t GetModuleDir(char* buf, size_t bufChars);

    // 启动事件驱动热重载（Config.ini → Reload，Whitelist/Blacklist.ini → Lists::LoadAll）
    void StartHotReload();
    void StopHotReload();
}
