#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Config.h"
#include "Logger.h"
#include "Whitelist.h"
#include <cstring>

namespace Config
{
    // MSVC linker intrinsic — the address of this symbol is the DLL's
    // HMODULE. We use it to anchor Config.ini next to *this* DLL instead of
    // the host EXE. Declared at file scope so the `extern "C"` is valid.
    extern "C" IMAGE_DOS_HEADER __ImageBase;

    std::atomic<bool> g_logEnabled{true};
    std::atomic<bool> g_pickupSuppressEnabled{true};

    static ULONGLONG g_lastCheck = 0;
    static ULONGLONG g_lastWrite = 0;
    static char      g_cachedPath[MAX_PATH];
    static bool      g_cachedPathReady = false;

    static HANDLE s_tickThread = nullptr;
    static HANDLE s_tickStop   = nullptr;

    size_t GetConfigPath(char* buf, size_t bufChars)
    {
        if (!buf || bufChars == 0) return 0;
        buf[0] = 0;

        // Cache: the DLL path never changes for a given process, so we
        // compute the full Config.ini path once and reuse it for every Tick.
        if (g_cachedPathReady) {
            size_t len = strlen(g_cachedPath);
            if (len + 1 > bufChars) return 0;
            memcpy(buf, g_cachedPath, len + 1);
            return len;
        }

        // Anchor Config.ini next to *this* DLL, not the host EXE. The
        // `&__ImageBase` trick gives us our own module handle without any
        // external state plumbing.
        HMODULE hMod = reinterpret_cast<HMODULE>(&__ImageBase);

        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(hMod, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return 0;

        // Strip to directory portion. path is null-terminated.
        char* lastSlash = nullptr;
        for (char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') lastSlash = p;
        if (lastSlash)
            lastSlash[1] = 0;
        else
            path[0] = 0;

        // Append "Config.ini".
        size_t dirLen = strlen(path);
        const char* suffix = "Config.ini";
        size_t sufLen = strlen(suffix);
        if (dirLen + sufLen + 1 > sizeof(g_cachedPath)) return 0;
        memcpy(g_cachedPath, path, dirLen);
        memcpy(g_cachedPath + dirLen, suffix, sufLen + 1); // include null terminator
        g_cachedPathReady = true;

        if (dirLen + sufLen + 1 > bufChars) return 0;
        memcpy(buf, g_cachedPath, dirLen + sufLen + 1);
        return dirLen + sufLen;
    }

    // Locate the line value for `key` inside the `[section]` block of `content`.
    // On hit, copies up to `bufChars - 1` bytes of the value (trimmed) into `buf`
    // and returns true. Returns false when the key/section is missing.
    static bool ReadIniValue(const char* content,
                             const char* section,
                             const char* key,
                             char* buf,
                             size_t bufChars)
    {
        if (!content || !section || !key || !buf || bufChars == 0) return false;
        buf[0] = 0;

        // Build "[section]" header.
        char header[128];
        size_t secLen = strlen(section);
        if (secLen + 2 >= sizeof(header)) return false;
        header[0] = '[';
        memcpy(header + 1, section, secLen);
        header[secLen + 1] = ']';
        header[secLen + 2] = 0;

        const char* p = strstr(content, header);
        if (!p) return false;

        // Find end of this section (start of next "[" header, or EOF).
        const char* sectionStart = p;
        const char* searchPos = p + strlen(header);
        const char* sectionEnd = content + strlen(content);
        while (true) {
            const char* nl = strchr(searchPos, '\n');
            if (!nl) break;
            const char* nextLine = nl + 1;
            while (*nextLine == ' ' || *nextLine == '\t' || *nextLine == '\r')
                ++nextLine;
            if (*nextLine == '[') {
                sectionEnd = nl;
                break;
            }
            searchPos = nl + 1;
        }

        // Walk lines inside the section.
        const char* pos = sectionStart + strlen(header);
        size_t keyLen = strlen(key);
        while (pos < sectionEnd) {
            // Skip leading newlines/whitespace.
            while (pos < sectionEnd && (*pos == '\r' || *pos == '\n'))
                ++pos;
            if (pos >= sectionEnd) break;

            // Find end of this line.
            const char* lineEnd = pos;
            while (lineEnd < sectionEnd && *lineEnd != '\r' && *lineEnd != '\n')
                ++lineEnd;

            // Skip pure comment lines.
            if (*pos == ';' || *pos == '#') {
                pos = lineEnd + 1;
                continue;
            }

            // Look for '='.
            const char* eq = nullptr;
            for (const char* q = pos; q < lineEnd; ++q)
                if (*q == '=') { eq = q; break; }
            if (!eq) {
                pos = lineEnd + 1;
                continue;
            }

            // Trim whitespace on key.
            const char* kBegin = pos;
            const char* kEnd = eq;
            while (kBegin < kEnd && (*kBegin == ' ' || *kBegin == '\t')) ++kBegin;
            while (kEnd > kBegin && (*(kEnd - 1) == ' ' || *(kEnd - 1) == '\t')) --kEnd;

            size_t thisKeyLen = (size_t)(kEnd - kBegin);
            if (thisKeyLen != keyLen) {
                pos = lineEnd + 1;
                continue;
            }
            if (memcmp(kBegin, key, keyLen) != 0) {
                pos = lineEnd + 1;
                continue;
            }

            // Trim value, strip an inline `;` or `#` comment, and copy out.
            const char* vBegin = eq + 1;
            const char* vEnd = lineEnd;
            while (vBegin < vEnd && (*vBegin == ' ' || *vBegin == '\t')) ++vBegin;
            while (vEnd > vBegin && (*(vEnd - 1) == ' ' || *(vEnd - 1) == '\t')) --vEnd;
            // Truncate at the first `;` or `#` so `Value = 0// note` parses as `0`.
            for (const char* c = vBegin; c < vEnd; ++c) {
                if (*c == ';' || *c == '#') { vEnd = c; break; }
            }
            while (vEnd > vBegin && (*(vEnd - 1) == ' ' || *(vEnd - 1) == '\t')) --vEnd;

            size_t valLen = (size_t)(vEnd - vBegin);
            if (valLen >= bufChars) valLen = bufChars - 1;
            memcpy(buf, vBegin, valLen);
            buf[valLen] = 0;
            return true;
        }
        return false;
    }

    // ASCII-only case-insensitive equality. Avoids pulling <string.h> macros
    // (_stricmp) that vary between CRT versions.
    static bool EqI(const char* a, const char* b)
    {
        while (*a && *b) {
            int ca = (int)(unsigned char)*a;
            int cb = (int)(unsigned char)*b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return false;
            ++a; ++b;
        }
        return *a == 0 && *b == 0;
    }

    static bool ParseBool(const char* v, bool defaultVal)
    {
        if (!v || !*v) return defaultVal;
        if (EqI(v, "1") || EqI(v, "true") || EqI(v, "yes") || EqI(v, "on"))  return true;
        if (EqI(v, "0") || EqI(v, "false") || EqI(v, "no")  || EqI(v, "off")) return false;
        return defaultVal;
    }

    // Read the entire file into a heap buffer. Caller frees with delete[].
    // Returns nullptr on open failure.
    static char* ReadFileToBuffer(const char* path, size_t* outSize)
    {
        if (outSize) *outSize = 0;
        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) return nullptr;

        DWORD size = GetFileSize(h, NULL);
        if (size == INVALID_FILE_SIZE || size == 0) {
            CloseHandle(h);
            return nullptr;
        }

        char* buf = new char[size + 1];
        DWORD got = 0;
        BOOL ok = ReadFile(h, buf, size, &got, NULL);
        CloseHandle(h);
        if (!ok || got != size) {
            delete[] buf;
            return nullptr;
        }
        buf[got] = 0;

        // Strip a leading BOM if the file was saved as UTF-8 with BOM. We
        // hand the caller a pointer past the BOM and a reduced byte count so
        // downstream strstr/strchr keep matching "[section]" as-is.
        size_t skip = 0;
        if (got >= 3 &&
            (unsigned char)buf[0] == 0xEF &&
            (unsigned char)buf[1] == 0xBB &&
            (unsigned char)buf[2] == 0xBF)
        {
            skip = 3;
        }

        char* content = buf + skip;
        size_t contentLen = got - skip;

        // Shift the remaining bytes to the start of the buffer so delete[]
        // still frees the same allocation as content.
        memmove(buf, content, contentLen);
        buf[contentLen] = 0;
        if (outSize) *outSize = contentLen;
        return buf;
    }

    void Reload()
    {
        char path[MAX_PATH];
        if (GetConfigPath(path, sizeof(path)) == 0) {
            LOG_MSG("Config", "Cannot resolve Config.ini path");
            return;
        }

        size_t sz = 0;
        char* content = ReadFileToBuffer(path, &sz);
        if (!content) {
            LOG("Config", "Cannot open %s", path);
            return;
        }

        char val[64];

        if (ReadIniValue(content, "Log", "Value", val, sizeof(val))) {
            g_logEnabled.store(ParseBool(val, true), std::memory_order_release);
            Logger::g_logWriteEnabled.store(g_logEnabled.load(std::memory_order_acquire), std::memory_order_release);
        }

        if (ReadIniValue(content, "PickupSuppress", "Value", val, sizeof(val)))
            g_pickupSuppressEnabled.store(ParseBool(val, true), std::memory_order_release);

        LOG("Config", "Reload: Log=%d PickupSuppress=%d",
            (int)g_logEnabled.load(), (int)g_pickupSuppressEnabled.load());

        delete[] content;
    }

    void Tick()
    {
        ULONGLONG now = GetTickCount64();
        if (now - g_lastCheck < 1000)
            return;
        g_lastCheck = now;

        char path[MAX_PATH];
        if (GetConfigPath(path, sizeof(path)) == 0) return;

        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            FILETIME ft;
            if (GetFileTime(h, NULL, NULL, &ft)) {
                ULONGLONG wt = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                if (wt != g_lastWrite) {
                    g_lastWrite = wt;
                    LOG_MSG("Config", "File change detected, reloading...");
                    Reload();
                }
            }
            CloseHandle(h);
        }
    }

    static DWORD WINAPI TickThreadProc(LPVOID)
    {
        LOG_MSG("Config", "tick thread started");
        for (;;) {
            DWORD r = WaitForSingleObject(s_tickStop, 500);
            if (r == WAIT_OBJECT_0) return 0; // stop signaled

            Tick();
            Whitelist::Tick();
        }
    }

    void StartHotReload()
    {
        Reload();
        Whitelist::Load();

        // Seed each file's mtime so the first Tick() after startup doesn't
        // fire a spurious "File change detected" reload — g_lastWrite/s_lastWrite
        // start at 0 and would otherwise always compare unequal to the real
        // mtime.
        char path[MAX_PATH];
        if (GetConfigPath(path, sizeof(path)) > 0) {
            HANDLE h = CreateFileA(path, GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                FILETIME ft;
                if (GetFileTime(h, NULL, NULL, &ft))
                    g_lastWrite = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                CloseHandle(h);
            }
        }
        Whitelist::SeedMTime();

        LOG_MSG("Config", "Lazy hot-reload enabled");

        // Drive Tick from a background thread so file edits are picked up
        // even when no game events (pickups) are firing. The 1-second throttle
        // inside Tick keeps the per-tick work minimal.
        if (s_tickThread) return;
        s_tickStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!s_tickStop) return;
        s_tickThread = CreateThread(nullptr, 0, TickThreadProc,
                                    nullptr, 0, nullptr);
    }

    void StopHotReload()
    {
        if (!s_tickThread) return;
        SetEvent(s_tickStop);
        WaitForSingleObject(s_tickThread, 2000);
        CloseHandle(s_tickThread);
        s_tickThread = nullptr;
        CloseHandle(s_tickStop);
        s_tickStop = nullptr;
    }
}