#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>

namespace Config
{
    // 拾取提示框屏蔽开关 (Config.ini → [PickupSuppress])
    extern std::atomic<bool> g_pickupSuppressEnabled;

    // 日志开关 (Config.ini → [Log])
    extern std::atomic<bool> g_logEnabled;

    void Reload();

    // 返回 Config.ini 完整路径（DLL 同目录），失败返回 0。
    size_t GetConfigPath(char* buf, size_t bufChars);

    // 返回本 DLL 所在目录（含尾部反斜杠），结果缓存。
    size_t GetModuleDir(char* buf, size_t bufChars);

    // 启动事件驱动热重载（Config.ini → Reload，Whitelist.ini → Whitelist::Load）
    void StartHotReload();
    void StopHotReload();
}
