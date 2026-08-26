#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>

namespace Config
{
    // 拾取提示框屏蔽开关 (Config.ini → [PickupSuppress])
    // atomic: written by the hot-reload thread, read by the game thread.
    extern std::atomic<bool> g_pickupSuppressEnabled;

    // 日志开关 (Config.ini → [Log])
    extern std::atomic<bool> g_logEnabled;

    void Reload();
    // Returns the full path to Config.ini (next to the host module).
    // Caller-provided buffer; returns the number of bytes written (excluding
    // the null terminator), or 0 on failure.
    size_t GetConfigPath(char* buf, size_t bufChars);

    // Background hot-reload — owns its own polling thread so changes are
    // picked up even when no game events are firing. Spawned by
    // StartHotReload, joined by StopHotReload.
    void Tick();
    void StartHotReload();
    void StopHotReload();
}