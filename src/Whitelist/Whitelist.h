#pragma once

namespace Whitelist
{
    // 解析 Whitelist.ini 写入备用快照后原子翻转；打开/读取失败保留旧快照。
    void Load();

    // 游戏线程查询：只读当前快照，无锁、零文件 IO。
    bool IsPickupAllowed(const char* name);
}
