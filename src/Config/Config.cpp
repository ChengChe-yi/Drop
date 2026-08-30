#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Config.h"
#include "Ini.h"
#include "Watcher.h"
#include "Logger.h"
#include "Whitelist.h"
#include <cstring>

namespace Config
{
    // __ImageBase 的地址即本 DLL 的 HMODULE，用于把配置锚定在 DLL 侧而非宿主 EXE。
    extern "C" IMAGE_DOS_HEADER __ImageBase;

    std::atomic<bool> g_logEnabled{true};
    std::atomic<bool> g_pickupSuppressEnabled{true};
    std::atomic<bool> g_inteeProbeEnabled{true};

    static char      g_cachedDir[MAX_PATH];
    static bool      g_cachedDirReady = false;

    size_t GetModuleDir(char* buf, size_t bufChars)
    {
        if (!buf || bufChars == 0) return 0;
        buf[0] = 0;

        if (g_cachedDirReady) {
            size_t len = strlen(g_cachedDir);
            if (len + 1 > bufChars) return 0;
            memcpy(buf, g_cachedDir, len + 1);
            return len;
        }

        HMODULE hMod = reinterpret_cast<HMODULE>(&__ImageBase);
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(hMod, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return 0;

        // 截到最后一个路径分隔符（保留尾部反斜杠）。
        char* lastSlash = nullptr;
        for (char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') lastSlash = p;
        if (!lastSlash) return 0;

        size_t dirLen = (size_t)(lastSlash - path) + 1;
        if (dirLen + 1 > sizeof(g_cachedDir)) return 0;
        memcpy(g_cachedDir, path, dirLen);
        g_cachedDir[dirLen] = 0;
        g_cachedDirReady = true;

        if (dirLen + 1 > bufChars) return 0;
        memcpy(buf, g_cachedDir, dirLen + 1);
        return dirLen;
    }

    size_t GetConfigPath(char* buf, size_t bufChars)
    {
        char dir[MAX_PATH];
        size_t dirLen = GetModuleDir(dir, sizeof(dir));
        if (dirLen == 0) return 0;

        const char* suffix = "Config.ini";
        size_t sufLen = strlen(suffix);
        if (dirLen + sufLen + 1 > bufChars) return 0;
        memcpy(buf, dir, dirLen);
        memcpy(buf + dirLen, suffix, sufLen + 1);
        return dirLen + sufLen;
    }

    // 在 watcher 线程执行：纯读文件 + 原子写，无锁、不触碰 loader。
    void Reload()
    {
        char path[MAX_PATH];
        if (GetConfigPath(path, sizeof(path)) == 0) {
            LOG_MSG("Config", "Cannot resolve Config.ini path");
            return;
        }

        size_t sz = 0;
        char* content = Ini::LoadFile(path, &sz);
        if (!content) {
            LOG("Config", "Cannot open %s", path);
            return;
        }

        char val[64];

        if (Ini::GetValue(content, "Log", "Value", val, sizeof(val))) {
            g_logEnabled.store(Ini::ParseBool(val, true), std::memory_order_release);
            Logger::g_logWriteEnabled.store(g_logEnabled.load(std::memory_order_acquire), std::memory_order_release);
        }

        if (Ini::GetValue(content, "PickupSuppress", "Value", val, sizeof(val)))
            g_pickupSuppressEnabled.store(Ini::ParseBool(val, true), std::memory_order_release);

        if (Ini::GetValue(content, "InteeProbe", "Value", val, sizeof(val)))
            g_inteeProbeEnabled.store(Ini::ParseBool(val, true), std::memory_order_release);

        LOG("Config", "Reload: Log=%d PickupSuppress=%d InteeProbe=%d",
            (int)g_logEnabled.load(), (int)g_pickupSuppressEnabled.load(),
            (int)g_inteeProbeEnabled.load());

        delete[] content;
    }

    void StartHotReload()
    {
        // 初始加载在 worker 线程完成；watcher 随后播种 mtime，首次检查不会误触发。
        Reload();
        Whitelist::Load();

        char dir[MAX_PATH];
        if (GetModuleDir(dir, sizeof(dir)) == 0) {
            LOG_MSG("Config", "Cannot resolve module dir, hot reload disabled");
            return;
        }

        if (Watcher::Start(dir, &Reload, &Whitelist::Load))
            LOG_MSG("Config", "Hot reload enabled (event-driven, 200ms debounce)");
        else
            LOG_MSG("Config", "Watcher failed to start, hot reload disabled");
    }

    void StopHotReload()
    {
        Watcher::Stop();
    }
}
