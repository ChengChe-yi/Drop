#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Whitelist.h"
#include "Ini.h"
#include "Config.h"
#include "Logger.h"
#include <atomic>
#include <cstring>

namespace Whitelist
{
    static constexpr size_t kMaxEntries = 256;
    static constexpr size_t kEntryLen   = 128;

    // 双缓冲快照：watcher 线程是唯一写者，填 inactive 块后 release 翻转索引；
    // 读取方 acquire 一次锁定快照，无锁无撕裂。极端情况（两次重载快于一次
    // 256 条扫描）最坏是单次瞬时误比较——128 字节槽内必有 NUL，不会越界；
    // 且防抖 200ms 使该情形实际不可达。
    struct Snapshot
    {
        char    items[kMaxEntries][kEntryLen];
        size_t  count;
    };

    static Snapshot              g_snap[2] = {};
    static std::atomic<uint32_t> g_active{0};

    static char s_filePath[MAX_PATH];
    static bool s_filePathReady = false;

    static const char* GetFilePath()
    {
        if (s_filePathReady) return s_filePath;

        char dir[MAX_PATH];
        if (Config::GetModuleDir(dir, sizeof(dir)) == 0) {
            s_filePath[0] = 0;
            s_filePathReady = true;
            return s_filePath;
        }

        strcpy_s(s_filePath, dir);
        strcat_s(s_filePath, "Whitelist.ini");
        s_filePathReady = true;
        return s_filePath;
    }

    // Ini::ForEachEntry 回调：把名单条目填进备用快照。
    static bool FillEntry(const char* value, void* ctx)
    {
        Snapshot* s = (Snapshot*)ctx;
        if (s->count >= kMaxEntries) return false;   // 名单满，提前终止。
        strcpy_s(s->items[s->count], kEntryLen, value);
        ++s->count;
        return true;
    }

    void Load()
    {
        const char* path = GetFilePath();

        // 小文件场景；1 MiB 上限防失控文件。读取失败保留当前快照。
        size_t sz = 0;
        char* content = Ini::LoadFile(path, &sz, 1 * 1024 * 1024);
        if (!content)
            return;

        // 解析进 inactive 快照，然后翻转——读取方永远看不到半填充状态。
        const uint32_t cur = g_active.load(std::memory_order_relaxed);
        Snapshot& scratch = g_snap[cur ^ 1];
        Ini::ForEachEntry(content, "PickupSuppress", &FillEntry, &scratch);
        g_active.store(cur ^ 1, std::memory_order_release);

        LOG("Whitelist", "Loaded %zu items (snapshot %u)", scratch.count, (unsigned)(cur ^ 1));

        delete[] content;
    }

    bool IsPickupAllowed(const char* name)
    {
        if (!name || !*name) return false;

        // 一次 acquire 锁定整份快照；写者翻转前只会动另一块。
        const uint32_t idx = g_active.load(std::memory_order_acquire);
        const Snapshot& s = g_snap[idx];

        for (size_t i = 0; i < s.count; ++i) {
            if (strcmp(s.items[i], name) == 0)
                return true;
        }
        return false;
    }
}
