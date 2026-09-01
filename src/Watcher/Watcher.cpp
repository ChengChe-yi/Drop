#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Watcher.h"
#include "Logger.h"
#include <cstdint>
#include <cstring>

namespace Watcher
{
    // 编辑器保存并非原子操作（VSCode 临时文件 + rename，Notepad 截断重写），
    // 一次保存产生一串通知；等安静窗口后对比 mtime，只重载最终的完整内容。
    static constexpr DWORD kDebounceMs = 200;
    static constexpr DWORD kPollFallbackMs = 1000;

    static HANDLE s_thread = nullptr;
    static HANDLE s_stop   = nullptr;
    static wchar_t s_dirW[MAX_PATH] = {};

    struct Tracked
    {
        const wchar_t* name;   // 仅文件名，不含路径
        void (*fire)();        // watcher 线程上执行的回调
        ULONGLONG mtime;
        bool      seeded;
    };

    // 顺序约定：[0] → config 回调，[1] → whitelist 回调，[2] → blacklist 回调。
    static Tracked s_files[3] = {};

    static void JoinPath(const wchar_t* name, wchar_t* out, size_t outChars)
    {
        size_t dirLen = wcslen(s_dirW);
        size_t nameLen = wcslen(name);
        if (dirLen + nameLen + 1 > outChars) {
            out[0] = 0;
            return;
        }
        memcpy(out, s_dirW, dirLen * sizeof(wchar_t));
        memcpy(out + dirLen, name, (nameLen + 1) * sizeof(wchar_t));
    }

    static ULONGLONG GetMTime(const wchar_t* path)
    {
        HANDLE h = CreateFileW(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return 0;
        FILETIME ft = {};
        ULONGLONG result = 0;
        if (GetFileTime(h, nullptr, nullptr, &ft))
            result = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        CloseHandle(h);
        return result;
    }

    // 启动时播种 mtime（不触发回调），避免首次检查误报变更。
    static void Seed()
    {
        for (Tracked& f : s_files) {
            wchar_t path[MAX_PATH];
            JoinPath(f.name, path, MAX_PATH);
            f.mtime = GetMTime(path);
            f.seeded = true;
        }
    }

    static void CheckAndFire()
    {
        for (Tracked& f : s_files) {
            wchar_t path[MAX_PATH];
            JoinPath(f.name, path, MAX_PATH);
            ULONGLONG mt = GetMTime(path);
            if (!f.seeded) {
                f.mtime = mt;
                f.seeded = true;
                continue;
            }
            if (mt != f.mtime) {
                f.mtime = mt;
                LOG("Watcher", "change detected: %ls", f.name);
                if (f.fire)
                    f.fire();
            }
        }
    }

    static wchar_t AsciiLower(wchar_t c)
    {
        return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c;
    }

    static bool NameEqualsI(const wchar_t* a, const wchar_t* b, size_t chars)
    {
        for (size_t i = 0; i < chars; ++i)
            if (AsciiLower(a[i]) != AsciiLower(b[i]))
                return false;
        return true;
    }

    // 通知批次里是否出现被监听的文件名。
    static bool BufferMentionsWatched(const BYTE* buf, DWORD bytes)
    {
        if (bytes == 0)
            return true;   // 缓冲溢出：无法解析，宁可多查一次。

        const BYTE* p = buf;
        const BYTE* end = buf + bytes;
        while (p + sizeof(DWORD) * 3 <= end) {
            const FILE_NOTIFY_INFORMATION* ni =
                (const FILE_NOTIFY_INFORMATION*)p;
            size_t nameChars = ni->FileNameLength / sizeof(WCHAR);
            for (const Tracked& f : s_files) {
                size_t wantLen = wcslen(f.name);
                if (nameChars == wantLen &&
                    NameEqualsI(ni->FileName, f.name, wantLen))
                    return true;
            }
            if (ni->NextEntryOffset == 0)
                break;
            p += ni->NextEntryOffset;
        }
        return false;
    }

    static DWORD WINAPI ThreadProc(LPVOID)
    {
        LOG_MSG("Watcher", "thread started");

        Seed();

        HANDLE dir = CreateFileW(s_dirW, FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE |
                                     FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS |
                                     FILE_FLAG_OVERLAPPED,
                                 nullptr);
        OVERLAPPED ovl = {};
        ovl.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset
        BYTE buffer[4096] alignas(DWORD);

        bool eventDriven = (dir != INVALID_HANDLE_VALUE && ovl.hEvent != nullptr);
        if (!eventDriven)
            LOG_MSG("Watcher", "dir handle unavailable, polling fallback");

        for (;;)
        {
            if (eventDriven)
            {
                DWORD bytes = 0;
                if (!ReadDirectoryChangesW(dir, buffer, sizeof(buffer), FALSE,
                                           FILE_NOTIFY_CHANGE_LAST_WRITE |
                                               FILE_NOTIFY_CHANGE_FILE_NAME |
                                               FILE_NOTIFY_CHANGE_SIZE,
                                           &bytes, &ovl, nullptr))
                {
                    LOG_MSG("Watcher", "RDCW failed, polling fallback");
                    eventDriven = false;
                    continue;
                }

                HANDLE waits[2] = { s_stop, ovl.hEvent };
                DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (r == WAIT_OBJECT_0 || r == WAIT_FAILED)
                    break;                                  // 停止信号（或句柄失效，兜底退出）
                if (r != WAIT_OBJECT_0 + 1) {
                    LOG("Watcher", "wait failed %lu, polling fallback", r);
                    eventDriven = false;
                    continue;
                }

                // 异步调用下 RDCW 的 lpBytesReturned 未定义，实际字节数要从
                // overlapped 结果取；取不到按缓冲不可解析处理，宁可多查一次。
                if (!GetOverlappedResult(dir, &ovl, &bytes, FALSE))
                    bytes = 0;

                if (!BufferMentionsWatched(buffer, bytes))
                    continue;   // 游戏目录里其他文件的读写，忽略。

                // 防抖等待；停止事件可立即打断，不必等满 200ms。
                if (WaitForSingleObject(s_stop, kDebounceMs) == WAIT_OBJECT_0)
                    break;

                CheckAndFire();
                // 防抖期间排队的通知会在下一轮立即浮出，自然合并。
            }
            else
            {
                DWORD r = WaitForSingleObject(s_stop, kPollFallbackMs);
                if (r == WAIT_OBJECT_0 || r == WAIT_FAILED)
                    break;
                CheckAndFire();
            }
        }

        if (ovl.hEvent) {
            if (dir != INVALID_HANDLE_VALUE)
                CancelIoEx(dir, &ovl);
            CloseHandle(ovl.hEvent);
        }
        if (dir != INVALID_HANDLE_VALUE)
            CloseHandle(dir);

        LOG_MSG("Watcher", "thread stopped");
        return 0;
    }

    bool Start(const wchar_t* dir,
               void (*onConfigChange)(),
               void (*onWhitelistChange)(),
               void (*onBlacklistChange)())
    {
        if (s_thread)
            return true;   // 已在运行

        if (!dir || !*dir ||
            !onConfigChange || !onWhitelistChange || !onBlacklistChange)
            return false;

        if (wcslen(dir) >= MAX_PATH)
            return false;

        wcscpy_s(s_dirW, dir);

        s_files[0] = { L"Config.ini",    onConfigChange,    0, false };
        s_files[1] = { L"Whitelist.ini", onWhitelistChange, 0, false };
        s_files[2] = { L"Blacklist.ini", onBlacklistChange, 0, false };

        s_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!s_stop)
            return false;

        s_thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
        if (!s_thread) {
            CloseHandle(s_stop);
            s_stop = nullptr;
            return false;
        }
        return true;
    }

    void Stop()
    {
        if (!s_thread)
            return;
        SetEvent(s_stop);
        const DWORD r = WaitForSingleObject(s_thread, 3000);
        CloseHandle(s_thread);
        s_thread = nullptr;
        if (r == WAIT_OBJECT_0) {
            CloseHandle(s_stop);
            s_stop = nullptr;
        }
    }
}
