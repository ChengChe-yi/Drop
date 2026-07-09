#include "pch.h"
#include "Config.h"
#include <fstream>
#include <sstream>
#include <algorithm>


static DropConfig      g_config = {
    true,           // pillarFilterEnabled
    false,          // suppressPickupBar
    true,           // isBlacklist

    { L"SceneObj_DropItem_Monster(Clone)",
      L"SceneObj_DropItem_Exp_Avatar(Clone)" },
    1000            // hotReloadMs
};
static CRITICAL_SECTION g_cs;
static bool            g_csInit = false;
static HANDLE          g_hReloadThread = nullptr;
static volatile bool   g_reloadRunning = false;
static uint64_t        g_lastFileTime = 0;



static std::string Trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}


static bool ReadFile(const std::string& path, std::string& out)
{
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}


static uint64_t GetFileTimeUs(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &info))
        return 0;
    return (uint64_t)info.ftLastWriteTime.dwLowDateTime |
           ((uint64_t)info.ftLastWriteTime.dwHighDateTime << 32);
}


static std::string GetIniValue(const std::string& ini, const std::string& section, const std::string& key)
{

    std::string secTag = "[" + section + "]";
    size_t secPos = ini.find(secTag);
    if (secPos == std::string::npos) return "";


    size_t endPos = ini.find("\n[", secPos + 1);
    if (endPos == std::string::npos) endPos = ini.size();

    std::string secContent = ini.substr(secPos, endPos - secPos);

    std::string searchKey = "Value";
    size_t eqPos = std::string::npos;
    size_t cur = 0;
    while (true)
    {
        size_t lineStart = secContent.find_first_not_of("\r\n", cur);
        if (lineStart == std::string::npos) break;
        size_t lineEnd = secContent.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string::npos) lineEnd = secContent.size();

        std::string line = secContent.substr(lineStart, lineEnd - lineStart);
        size_t colon = line.find('=');
        if (colon != std::string::npos)
        {
            std::string k = Trim(line.substr(0, colon));
            if (k == searchKey)
            {
                return Trim(line.substr(colon + 1));
            }
        }

        cur = lineEnd + 1;
    }

    return "";
}


static bool GetIniBool(const std::string& ini, const std::string& section, bool def)
{
    std::string v = GetIniValue(ini, section, "Value");
    if (v.empty()) return def;
    return (v == "1" || v == "true" || v == "True");
}


static int GetIniInt(const std::string& ini, const std::string& section, int def)
{
    std::string v = GetIniValue(ini, section, "Value");
    if (v.empty()) return def;
    return std::stoi(v);
}


static std::vector<std::wstring> GetIniStringList(const std::string& ini, const std::string& section)
{
    std::vector<std::wstring> result;
    std::string val = GetIniValue(ini, section, "Value");
    if (val.empty()) return result;

    size_t start = 0;
    while (true)
    {
        size_t end = val.find(',', start);
        std::string item = Trim(val.substr(start, end - start));
        if (!item.empty())
        {
            // UTF-8 → wchar_t
            int wlen = MultiByteToWideChar(CP_UTF8, 0, item.c_str(), -1, nullptr, 0);
            if (wlen > 0)
            {
                std::vector<wchar_t> wbuf(wlen);
                MultiByteToWideChar(CP_UTF8, 0, item.c_str(), -1, wbuf.data(), wlen);
                result.push_back(std::wstring(wbuf.data()));
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }

    return result;
}

namespace Config
{
    std::string GetConfigPath()
    {
        char path[MAX_PATH];
        HMODULE hm = nullptr;
 
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&g_config, &hm);
        GetModuleFileNameA(hm, path, MAX_PATH);

        std::string full(path);
        size_t slash = full.find_last_of("\\/");
        if (slash != std::string::npos)
            full = full.substr(0, slash);

        return full + "\\Drop.ini";
    }

    void Reload()
    {
        std::string path = GetConfigPath();
        std::string ini;

        if (!ReadFile(path, ini))
        {
            OutputDebugStringA("[Drop] Drop.ini not found, using defaults\n");
            return;
        }

        DropConfig cfg;

        // [PillarFilter] — 屏蔽总开关
        cfg.pillarFilterEnabled = GetIniBool(ini, "PillarFilter", true);

        // [SuppressPickupBar] — 屏蔽拾取栏
        cfg.suppressPickupBar = GetIniBool(ini, "SuppressPickupBar", false);

        // [FilterMode] — 黑名单/白名单
        std::string mode = GetIniValue(ini, "FilterMode", "Value");
        cfg.isBlacklist = (mode != "whitelist");

        // [Blacklist] — 名字列表
        cfg.filterNames = GetIniStringList(ini, "Blacklist");

        // 如果 [Blacklist] 没读到, 尝试 [Whitelist]
        if (cfg.filterNames.empty())
        {
            cfg.filterNames = GetIniStringList(ini, "Whitelist");
        }

        // [HotReload] 间隔
        int ms = GetIniInt(ini, "HotReload", 1000);
        if (ms >= 100) cfg.hotReloadMs = ms;


        if (g_csInit) EnterCriticalSection(&g_cs);
        g_config = cfg;
        if (g_csInit) LeaveCriticalSection(&g_cs);

        OutputDebugStringA("[Drop] Config reloaded\n");
    }

    DropConfig Get()
    {
        if (!g_csInit) return g_config;
        EnterCriticalSection(&g_cs);
        DropConfig c = g_config;
        LeaveCriticalSection(&g_cs);
        return c;
    }

    static DWORD WINAPI ReloadThread(LPVOID)
    {
        std::string path = GetConfigPath();
        g_lastFileTime = GetFileTimeUs(path);
        g_reloadRunning = true;

        while (g_reloadRunning)
        {
            Sleep(g_config.hotReloadMs);

            uint64_t ft = GetFileTimeUs(path);
            if (ft != 0 && ft != g_lastFileTime)
            {
                g_lastFileTime = ft;
                Reload();
                OutputDebugStringA("[Drop] Config hot-reloaded\n");
            }
        }

        return 0;
    }

    void StartHotReload()
    {
        if (g_hReloadThread) return;

        if (!g_csInit)
        {
            InitializeCriticalSection(&g_cs);
            g_csInit = true;
        }

        Reload();
        g_hReloadThread = CreateThread(nullptr, 0, ReloadThread, nullptr, 0, nullptr);
    }

    void StopHotReload()
    {
        g_reloadRunning = false;
        if (g_hReloadThread)
        {
            WaitForSingleObject(g_hReloadThread, 5000);
            CloseHandle(g_hReloadThread);
            g_hReloadThread = nullptr;
        }

        if (g_csInit)
        {
            DeleteCriticalSection(&g_cs);
            g_csInit = false;
        }
    }
}
