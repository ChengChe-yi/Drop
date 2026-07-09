#pragma once
#include <string>
#include <vector>

struct DropConfig
{
    bool pillarFilterEnabled = true;
    bool suppressPickupBar = false;
    bool isBlacklist = true;
    std::vector<std::wstring> filterNames;
    int hotReloadMs = 1000;
};

namespace Config
{
    DropConfig Get();
    void Reload();
    std::string GetConfigPath();
    void StartHotReload();
    void StopHotReload();
}
