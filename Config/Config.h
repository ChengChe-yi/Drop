#pragma once

namespace Config
{
    // 总开关 — [PillarFilter] Value，关闭时所有拦截均跳过
    extern bool g_pillarFilterEnabled;

    // 日志开关 (Config.ini → [Log])
    extern bool g_logEnabled;

    // 光柱屏蔽开关 (Config.ini → [PillarSuppress])
    extern bool g_pillarSuppressEnabled;

    // 拾取提示框屏蔽开关 (Config.ini → [PickupSuppress])
    extern bool g_pickupSuppressEnabled;

    // DLL模块句柄（由 DllMain 设置，手动映射时用）
    extern HMODULE g_hModule;

    void Reload();
    std::string GetConfigPath();

    // Lazy hot-reload poll — call from hook handlers on the game thread.
    // Polls Config.ini mtime at most once per second; reloads on change.
    void Tick();
    void StartHotReload();
    void StopHotReload();
}
