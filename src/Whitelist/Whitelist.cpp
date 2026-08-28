#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Whitelist.h"
#include "Config.h"
#include "Logger.h"
#include <atomic>
#include <cstdint>
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

    // 拷贝 [start, end) 去首尾空白后写入 out，返回拷贝字节数。
    static size_t CopyTrimmed(const char* start, const char* end,
                              char* out, size_t outChars)
    {
        if (outChars == 0) return 0;
        while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
            ++start;
        while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t' || *(end - 1) == '\r' || *(end - 1) == '\n'))
            --end;
        size_t len = (size_t)(end - start);
        if (len >= outChars) len = outChars - 1;
        memcpy(out, start, len);
        out[len] = 0;
        return len;
    }

    // 在 content 中查找 "[section]" 头，返回指向 '[' 的指针或 nullptr。
    static const char* FindSection(const char* content, const char* section)
    {
        char header[128];
        size_t secLen = strlen(section);
        if (secLen + 2 >= sizeof(header)) return nullptr;
        header[0] = '[';
        memcpy(header + 1, section, secLen);
        header[secLen + 1] = ']';
        header[secLen + 2] = 0;
        return strstr(content, header);
    }

    // 返回 section 的结束位置（下一个 "[" 行头之前，或 EOF）。
    static const char* FindSectionEnd(const char* headerStart)
    {
        const char* searchPos = strchr(headerStart, '\n');
        if (!searchPos) return headerStart + strlen(headerStart);
        const char* sectionEnd = headerStart + strlen(headerStart);
        while (searchPos) {
            const char* nextLine = searchPos + 1;
            while (*nextLine == ' ' || *nextLine == '\t' || *nextLine == '\r')
                ++nextLine;
            if (*nextLine == '[') {
                sectionEnd = searchPos;
                break;
            }
            searchPos = strchr(nextLine, '\n');
        }
        return sectionEnd;
    }

    static void ParseSection(const char* content,
                             const char* section,
                             Snapshot& out)
    {
        out.count = 0;
        const char* hdr = FindSection(content, section);
        if (!hdr) return;

        const char* sectionEnd = FindSectionEnd(hdr);
        const char* pos = strchr(hdr, '\n');
        if (!pos) return;
        ++pos;

        while (pos < sectionEnd && out.count < kMaxEntries) {
            const char* nl = pos;
            while (nl < sectionEnd && *nl != '\n') ++nl;

            char trimmed[kEntryLen];
            CopyTrimmed(pos, nl, trimmed, sizeof(trimmed));

            if (trimmed[0] && trimmed[0] != ';' && trimmed[0] != '#') {
                memcpy(out.items[out.count], trimmed, kEntryLen);
                ++out.count;
            }

            pos = nl + 1;
        }
    }

    void Load()
    {
        const char* path = GetFilePath();

        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return;   // 文件缺失：保留当前快照。

        DWORD size = GetFileSize(h, NULL);
        if (size == INVALID_FILE_SIZE || size == 0) {
            CloseHandle(h);
            return;
        }

        // 小文件走栈缓冲，超过 1 MiB 直接放弃（防失控文件）。
        char stackBuf[8192];
        char* content = stackBuf;
        char* heapBuf = nullptr;
        if (size + 1 > sizeof(stackBuf)) {
            if (size + 1 > 1 * 1024 * 1024) {
                CloseHandle(h);
                return;
            }
            heapBuf = new char[size + 1];
            content = heapBuf;
        }

        DWORD got = 0;
        BOOL ok = ReadFile(h, content, size, &got, NULL);
        CloseHandle(h);
        if (!ok || got != size) {
            delete[] heapBuf;
            return;
        }
        content[got] = 0;

        // 跳过 UTF-8 BOM，保证编辑器保存后仍能匹配 "[PickupSuppress]"。
        if (got >= 3 &&
            (unsigned char)content[0] == 0xEF &&
            (unsigned char)content[1] == 0xBB &&
            (unsigned char)content[2] == 0xBF)
        {
            content += 3;
            got -= 3;
        }

        // 解析进 inactive 快照，然后翻转——读取方永远看不到半填充状态。
        const uint32_t cur = g_active.load(std::memory_order_relaxed);
        Snapshot& scratch = g_snap[cur ^ 1];
        ParseSection(content, "PickupSuppress", scratch);
        g_active.store(cur ^ 1, std::memory_order_release);

        LOG("Whitelist", "Loaded %zu items (snapshot %u)", scratch.count, (unsigned)(cur ^ 1));

        delete[] heapBuf;
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
