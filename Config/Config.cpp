#include "pch.h"
#include "Config.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <string>

namespace Config
{
    bool g_pillarFilterEnabled = false;

    static std::atomic<bool> g_running = false;
    static std::thread g_thread;

    std::string GetConfigPath()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(GetModuleHandleW(L"Drop.dll"), path, MAX_PATH);
        std::string exe(path);
        auto pos = exe.find_last_of("\\/");
        if (pos != std::string::npos)
            exe = exe.substr(0, pos + 1);
        return exe + "Drop.ini";
    }

  
    static int ReadIniValue(const std::string& content, const std::string& section)
    {
        std::string search = "[" + section + "]";
        auto s = content.find(search);
        if (s == std::string::npos) return -1;

        s = content.find("Value", s + search.size());
        if (s == std::string::npos) return -1;

        auto eq = content.find('=', s);
        if (eq == std::string::npos) return -1;

        eq++; 
        while (eq < content.size() && content[eq] == ' ') eq++;

        if (eq < content.size() && content[eq] == '1') return 1;
        if (eq < content.size() && content[eq] == '0') return 0;
        return -1;
    }

    void Reload()
    {
        g_pillarFilterEnabled = false;  
        std::string path = GetConfigPath();
        std::ifstream f(path);
        if (!f.is_open()) {
            OutputDebugStringA(("[Drop] Config not found: " + path + "\n").c_str());
            return;
        }
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

      
        char dbg[256];
        sprintf_s(dbg, "[Drop] Config content (100): %.100s", content.c_str());
        OutputDebugStringA(dbg);

        int v = ReadIniValue(content, "PillarFilter");
        if (v >= 0) g_pillarFilterEnabled = (v != 0);

        sprintf_s(dbg, "[Drop] Config: pillarFilter=%d path=%s",
                  g_pillarFilterEnabled, path.c_str());
        OutputDebugStringA(dbg);
    }

    static void WatchThread()
    {
        std::string path = GetConfigPath();
        ULONGLONG lastWrite = 0;

        while (g_running) {
            HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                FILETIME ft;
                if (GetFileTime(h, NULL, NULL, &ft)) {
                    ULONGLONG wt = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                    if (wt != lastWrite) {
                        lastWrite = wt;
                        Reload();
                    }
                }
                CloseHandle(h);
            }
            Sleep(1000);
        }
    }

    void StartHotReload()
    {
        Reload();
        if (g_running) return;
        g_running = true;
        g_thread = std::thread(WatchThread);
        g_thread.detach();
    }

    void StopHotReload()
    {
        g_running = false;
    }
}
