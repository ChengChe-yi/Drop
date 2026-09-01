#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Lists.h"
#include "Ini.h"
#include "Config.h"
#include "Logger.h"
#include <atomic>
#include <cstring>

namespace
{
    constexpr size_t kMaxEntries = 256;
    constexpr size_t kEntryLen   = 128;

    struct Section
    {
        char   items[kMaxEntries][kEntryLen];
        size_t count;
    };

    struct Snapshot
    {
        Section text;   // [Text]：精确匹配
        Section icon;   // [Icon]：精确匹配
    };

    struct FillCtx
    {
        Snapshot* snap;
        Section*  target;
    };

    bool FillEntryCb(const char* value, void* ctx)
    {
        FillCtx* fc = (FillCtx*)ctx;
        Section* s = fc->target;
        if (s->count >= kMaxEntries) return false;   // 名单满，提前终止。
        strcpy_s(s->items[s->count], kEntryLen, value);
        ++s->count;
        return true;
    }

    // 双缓冲名单快照：填 inactive 块后 release 翻转，读取方 acquire 锁定。
    class Table
    {
    public:
        // 解析 content 的名单区填入备用快照后翻转。textSecExtra 可选
        // （旧版段名兼容，如 Whitelist.ini 的 [PickupSuppress]）。
        void Load(const char* content, const char* tag,
                  const char* textSec, const char* iconSec,
                  const char* textSecExtra = nullptr)
        {
            const uint32_t cur = m_active.load(std::memory_order_relaxed);
            Snapshot& scratch = m_snap[cur ^ 1];
            scratch.text.count = 0;
            scratch.icon.count = 0;

            FillCtx fc { &scratch, &scratch.text };
            Ini::ForEachEntry(content, textSec, &FillEntryCb, &fc);
            if (textSecExtra) {
                fc.target = &scratch.text;
                Ini::ForEachEntry(content, textSecExtra, &FillEntryCb, &fc);
            }
            fc.target = &scratch.icon;
            Ini::ForEachEntry(content, iconSec, &FillEntryCb, &fc);

            m_active.store(cur ^ 1, std::memory_order_release);
            LOG(tag, "Loaded text=%zu icon=%zu", scratch.text.count, scratch.icon.count);
        }
        
        bool Match(const char* text, const char* icon) const
        {
            const uint32_t idx = m_active.load(std::memory_order_acquire);
            const Snapshot& s = m_snap[idx];

            if (text && *text) {
                for (size_t i = 0; i < s.text.count; ++i)
                    if (strcmp(s.text.items[i], text) == 0)
                        return true;
            }
            if (icon && *icon) {
                for (size_t i = 0; i < s.icon.count; ++i)
                    if (strcmp(icon, s.icon.items[i]) == 0)
                        return true;
            }
            return false;
        }

    private:
        Snapshot              m_snap[2] = {};
        std::atomic<uint32_t> m_active{0};
    };

    Table g_whitelist;
    Table g_blacklist;

    void LoadOne(const char* fileName, const char* tag,
                 const char* textSecExtra, Table& table)
    {
        char path[MAX_PATH];
        char dir[MAX_PATH];
        if (Config::GetModuleDir(dir, sizeof(dir)) == 0) return;
        strcpy_s(path, dir);
        strcat_s(path, fileName);

        size_t sz = 0;
        char* content = Ini::LoadFile(path, &sz, 1 * 1024 * 1024);
        if (!content) return;   // 文件缺失：保留当前快照。
        table.Load(content, tag, "Text", "Icon", textSecExtra);
        delete[] content;
    }
}

void Lists::LoadWhitelist()
{
    LoadOne("Whitelist.ini", "Whitelist", "PickupSuppress", g_whitelist);
}

void Lists::LoadBlacklist()
{
    LoadOne("Blacklist.ini", "Blacklist", nullptr, g_blacklist);
}

Lists::Verdict Lists::Evaluate(const char* text, const char* icon)
{
    if (Config::g_blacklistEnabled.load(std::memory_order_acquire) &&
        g_blacklist.Match(text, icon))
        return Verdict::Block;

    if (Config::g_whitelistEnabled.load(std::memory_order_acquire) &&
        g_whitelist.Match(text, icon))
        return Verdict::Allow;

    return Verdict::Default;
}

bool Lists::IsBlacklisted(const char* text, const char* icon)
{
    if (!Config::g_blacklistEnabled.load(std::memory_order_acquire))
        return false;
    return g_blacklist.Match(text, icon);
}
