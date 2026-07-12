#pragma once

namespace Config
{
    extern bool g_pillarFilterEnabled;  // 总开关

    void Reload();
    std::string GetConfigPath();
    void StartHotReload();
    void StopHotReload();
}
