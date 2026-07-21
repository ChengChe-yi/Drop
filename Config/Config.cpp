#include "pch.h"
#include "Config.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <atomic>
#include <string>

namespace Config
{
    bool g_logEnabled            = true;
    bool g_pillarSuppressEnabled = true;
    bool g_pickupSuppressEnabled = true;
    bool g_pillarFilterEnabled   = true;  

    static std::atomic<bool> g_running = false;
    static HANDLE g_stopEvent = nullptr;  // Event handle for clean shutdown
    static HANDLE g_threadHandle = nullptr;

    std::string GetConfigPath()
    {
        HMODULE hMod = g_hModule;
        if (!hMod)
            hMod = GetModuleHandleW(nullptr); 

        char path[MAX_PATH];
        GetModuleFileNameA(hMod, path, MAX_PATH);
        std::string exe(path);
        auto pos = exe.find_last_of("\\/");
        if (pos != std::string::npos)
            exe = exe.substr(0, pos + 1);
        return exe + "Config.ini";
    }


    static std::string ReadIniValue(const std::string& content, const std::string& section, const std::string& key)
    {
        std::string sectionHeader = "[" + section + "]";
        auto sectionStart = content.find(sectionHeader);
        if (sectionStart == std::string::npos)
            return {};


        // Find end of section: next [Section] at line start, or end of file
        auto sectionEnd = content.size();
        auto searchPos = sectionStart + sectionHeader.size();
        while (true) {
            auto nlPos = content.find('\n', searchPos);
            if (nlPos == std::string::npos) break;
            auto nextLine = content.find_first_not_of(" \t\r", nlPos + 1);
            if (nextLine != std::string::npos && nextLine < content.size() &&
                content[nextLine] == '[') {
                sectionEnd = nlPos;
                break;
            }
            searchPos = nlPos + 1;
        }

        auto pos = sectionStart + sectionHeader.size();

        while (pos < sectionEnd)
        {
            auto lineStart = content.find_first_not_of("\r\n", pos);
            if (lineStart == std::string::npos || lineStart > sectionEnd)
                break;

            auto lineEnd = content.find_first_of("\r\n", lineStart);
            if (lineEnd == std::string::npos)
                lineEnd = sectionEnd;


            if (content[lineStart] == ';' || content[lineStart] == '#')
            {
                pos = lineEnd + 1;
                continue;
            }

            std::string line = content.substr(lineStart, lineEnd - lineStart);
            auto eqPos = line.find('=');
            if (eqPos != std::string::npos)
            {
                std::string lineKey = line.substr(0, eqPos);
                lineKey.erase(0, lineKey.find_first_not_of(" \t"));
                lineKey.erase(lineKey.find_last_not_of(" \t") + 1);

                if (lineKey == key)
                {
                    std::string value = line.substr(eqPos + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    return value;
                }
            }

            pos = lineEnd + 1;
        }

        return {};
    }


    static bool ParseBool(const std::string& v, bool defaultVal)
    {
        if (v.empty()) return defaultVal;
        return (v == "1" || v == "true" || v == "yes");
    }

    void Reload()
    {
        std::string path = GetConfigPath();
        std::ifstream f(path);
        if (!f.is_open()) {
            LOG("Config", "Cannot open %s", path.c_str());
            return;
        }

        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        // [Log] — 日志开关
        g_logEnabled = ParseBool(ReadIniValue(content, "Log", "Value"), true);
        g_logWriteEnabled = g_logEnabled;  // sync Logger toggle

        // [PillarSuppress] — 光柱屏蔽
        g_pillarSuppressEnabled = ParseBool(ReadIniValue(content, "PillarSuppress", "Value"), true);

        // [PickupSuppress] — 拾取提示框屏蔽
        g_pickupSuppressEnabled = ParseBool(ReadIniValue(content, "PickupSuppress", "Value"), true);

        // [PillarFilter] — 旧总开关（兼容）
        std::string v = ReadIniValue(content, "PillarFilter", "Value");
        if (!v.empty())
            g_pillarFilterEnabled = ParseBool(v, true);
        else
            g_pillarFilterEnabled = ParseBool(ReadIniValue(content, "PillarFilter", "Enabled"), true);

        LOG("Config", "Reload: PillarFilter=%d Log=%d PillarSuppress=%d PickupSuppress=%d",
            (int)g_pillarFilterEnabled, (int)g_logEnabled, (int)g_pillarSuppressEnabled, (int)g_pickupSuppressEnabled);
    }

    static DWORD WINAPI WatchThreadProc(LPVOID)
    {
        std::string path = GetConfigPath();
        ULONGLONG lastWrite = 0;

        while (true)
        {
            // Wait for stop event with 1 second timeout
            if (WaitForSingleObject(g_stopEvent, 1000) == WAIT_OBJECT_0)
                break;  // stop signaled, exit cleanly

            HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                FILETIME ft;
                if (GetFileTime(h, NULL, NULL, &ft)) {
                    ULONGLONG wt = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                    if (wt != lastWrite) {
                        lastWrite = wt;
                        LOG_MSG("Config", "File change detected, reloading...");
                        Reload();
                    }
                }
                CloseHandle(h);
            }
        }
        return 0;
    }

    void StartHotReload()
    {
        Reload();
        if (g_running) return;
        g_running = true;

        // Create stop event (auto-reset)
        g_stopEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_stopEvent) {
            LOG_MSG("Config", "Failed to create stop event");
            g_running = false;
            return;
        }

        g_threadHandle = CreateThread(nullptr, 0, WatchThreadProc, nullptr, 0, nullptr);
        if (!g_threadHandle) {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
            g_running = false;
            LOG_MSG("Config", "Failed to create watch thread");
        }
        LOG_MSG("Config", "Hot-reload started");
    }

    void StopHotReload()
    {
        if (!g_running) return;
        g_running = false;

        // Signal the watch thread to exit
        if (g_stopEvent) {
            SetEvent(g_stopEvent);
        }

        // Wait for thread to exit (up to 3 seconds)
        if (g_threadHandle) {
            WaitForSingleObject(g_threadHandle, 3000);
            CloseHandle(g_threadHandle);
            g_threadHandle = nullptr;
        }
        if (g_stopEvent) {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
        LOG_MSG("Config", "Hot-reload stopped");
    }
}
