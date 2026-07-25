#include "pch.h"
#include "Whitelist.h"
#include "Config.h"
#include "Logger.h"
#include <fstream>
#include <string>
#include <vector>

namespace Whitelist
{
    static std::vector<std::string> g_pickupWhitelist;
    static std::string s_filePath;
    static ULONGLONG s_lastCheck = 0;
    static ULONGLONG s_lastWrite = 0;

    static std::string GetFilePath()
    {
        if (!s_filePath.empty())
            return s_filePath;

        std::string configPath = Config::GetConfigPath();
        auto pos = configPath.find_last_of("\\/");
        if (pos != std::string::npos)
            s_filePath = configPath.substr(0, pos + 1) + "Whitelist.ini";
        else
            s_filePath = "Whitelist.ini";
        return s_filePath;
    }

    static void ParseSection(const std::string& content,
                             const std::string& section,
                             std::vector<std::string>& out)
    {
        out.clear();

        std::string header = "[" + section + "]";
        auto start = content.find(header);
        if (start == std::string::npos)
            return;
        start += header.size();

        auto end = content.size();
        auto nextBracket = content.find("\n[", start);
        if (nextBracket != std::string::npos)
            end = nextBracket;

        size_t pos = start;
        while (pos < end)
        {
            auto nl = content.find('\n', pos);
            if (nl == std::string::npos)
                nl = end;

            std::string line = content.substr(pos, nl - pos);

            line.erase(0, line.find_first_not_of(" \t\r"));
            line.erase(line.find_last_not_of(" \t\r") + 1);


            if (!line.empty() && line[0] != ';' && line[0] != '#')
                out.push_back(line);

            pos = nl + 1;
        }
    }

    void Load()
    {
        std::string path = GetFilePath();
        std::ifstream f(path);
        if (!f.is_open())
        {
            g_pickupWhitelist.clear();
            return;
        }

        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        ParseSection(content, "PickupSuppress", g_pickupWhitelist);

        LOG("Whitelist", "Loaded %zu items from %s",
            g_pickupWhitelist.size(), path.c_str());
    }


    bool IsPickupAllowed(const char* name)
    {
        if (!name || !*name)
            return false;

        for (const auto& entry : g_pickupWhitelist)
        {
            if (entry == name)
                return true;
        }
        return false;
    }


    void Tick()
    {
        ULONGLONG now = GetTickCount64();
        if (now - s_lastCheck < 1000)
            return;
        s_lastCheck = now;

        std::string path = GetFilePath();

        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            FILETIME ft;
            if (GetFileTime(h, NULL, NULL, &ft))
            {
                ULONGLONG wt = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                if (wt != s_lastWrite)
                {
                    s_lastWrite = wt;
                    LOG_MSG("Whitelist", "File change detected, reloading...");
                    Load();
                }
            }
            CloseHandle(h);
        }
    }
}
