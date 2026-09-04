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

    // tag：名单名（Whitelist/Blacklist），what：区段名（Text/Icon）——仅用于告警日志。
    struct FillCtx
    {
        Section*    target;
        const char* tag;
        const char* what;
    };

    bool FillEntryCb(const char* value, void* ctx)
    {
        FillCtx* fc = (FillCtx*)ctx;
        Section* s = fc->target;
        if (s->count >= kMaxEntries) return false;   // 名单满，提前终止。

        const size_t len = strlen(value);
        if (len >= kEntryLen) {
            // 槽位定长 kEntryLen 字节（最多 127 字符 + NUL）。超长条目若硬拷，
            // strcpy_s 会触发 CRT invalid-parameter handler 直接终止进程；
            // 名单是用户可编辑数据，这里跳过并告警，保持 fail-soft。
            LOG(fc->tag, "[%s] #%zu too long (%zu >= %zu), skipped",
                fc->what, s->count, len, (size_t)kEntryLen);
            return true;
        }

        strcpy_s(s->items[s->count], kEntryLen, value);
        ++s->count;
        return true;
    }

    // SRWLOCK 保护的名单快照：Load 组装新快照后锁内替换，Match 共享锁读取。
    // 组装在锁外栈上完成，锁的持有时间只有一次结构拷贝。
    class Table
    {
    public:
        // 解析 content 的名单区填入备用快照后翻转。textSecExtra 可选
        // （旧版段名兼容，如 Whitelist.ini 的 [PickupSuppress]）。
        void Load(const char* content, const char* tag,
                  const char* textSec, const char* iconSec,
                  const char* textSecExtra = nullptr)
        {
            Snapshot next = {};

            FillCtx fc { &next.text, tag, textSec };
            Ini::ForEachEntry(content, textSec, &FillEntryCb, &fc);
            if (textSecExtra) {
                fc.target = &next.text;
                fc.what   = textSecExtra;
                Ini::ForEachEntry(content, textSecExtra, &FillEntryCb, &fc);
            }
            fc.target = &next.icon;
            fc.what   = iconSec;
            Ini::ForEachEntry(content, iconSec, &FillEntryCb, &fc);

            AcquireSRWLockExclusive(&m_lock);
            m_snap = next;
            ReleaseSRWLockExclusive(&m_lock);
            LOG(tag, "Loaded text=%zu icon=%zu", next.text.count, next.icon.count);
        }

        bool Match(const char* text, const char* icon) const
        {
            bool hit = false;

            AcquireSRWLockShared(&m_lock);
            const Snapshot& s = m_snap;

            if (text && *text) {
                for (size_t i = 0; i < s.text.count; ++i)
                    if (strcmp(s.text.items[i], text) == 0) {
                        hit = true;
                        break;
                    }
            }
            if (!hit && icon && *icon) {
                for (size_t i = 0; i < s.icon.count; ++i)
                    if (strcmp(icon, s.icon.items[i]) == 0) {
                        hit = true;
                        break;
                    }
            }

            ReleaseSRWLockShared(&m_lock);
            return hit;
        }

    private:
        Snapshot            m_snap = {};
        mutable SRWLOCK     m_lock = SRWLOCK_INIT;
    };

    Table g_whitelist;
    Table g_blacklist;

    void LoadOne(const wchar_t* fileName, const char* tag,
                 const char* textSecExtra, Table& table)
    {
        wchar_t path[MAX_PATH];
        wchar_t dir[MAX_PATH];
        if (Config::GetModuleDir(dir, sizeof(dir) / sizeof(wchar_t)) == 0) return;
        if (wcslen(dir) + wcslen(fileName) + 1 > MAX_PATH) return;
        wcscpy_s(path, dir);
        wcscat_s(path, fileName);

        size_t sz = 0;
        char* content = Ini::LoadFile(path, &sz, 1 * 1024 * 1024);
        if (!content) return;   // 文件缺失：保留当前快照。
        table.Load(content, tag, "Text", "Icon", textSecExtra);
        delete[] content;
    }
}

void Lists::LoadWhitelist()
{
    LoadOne(L"Whitelist.ini", "Whitelist", "PickupSuppress", g_whitelist);
}

void Lists::LoadBlacklist()
{
    LoadOne(L"Blacklist.ini", "Blacklist", nullptr, g_blacklist);
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
