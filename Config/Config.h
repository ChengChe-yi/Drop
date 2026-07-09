#pragma once
#include <string>
#include <vector>


struct DropConfig
{
    // 屏蔽总开关
    bool pillarFilterEnabled = true;

    // 是否也屏蔽拾取栏 (默认只屏蔽光柱)
    bool suppressPickupBar = false;

    // 过滤模式: true=黑名单, false=白名单
    bool isBlacklist = true;

    // 名字列表 (匹配 GameObject 名)
    std::vector<std::wstring> filterNames;

    // 热重载间隔 (毫秒)
    int hotReloadMs = 1000;  // 1秒
};

namespace Config
{
    DropConfig Get();
    void Reload();
    std::string GetConfigPath();
    void StartHotReload();
    void StopHotReload();
}
