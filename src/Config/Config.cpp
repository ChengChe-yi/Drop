#include "framework.h"
#include "Config.h"
#include "Logger.h"
#include <cstring>

namespace Config
{
    bool g_logEnabled            = true;
    bool g_pillarSuppressEnabled = true;
    bool g_pickupSuppressEnabled = true;
    bool g_pillarFilterEnabled   = true;

    HMODULE g_hModule = nullptr;

    static ULONGLONG g_lastCheck = 0;
    static ULONGLONG g_lastWrite = 0;

    size_t GetConfigPath(char* buf, size_t bufChars)
    {
        if (!buf || bufChars == 0) return 0;
        buf[0] = 0;

        HMODULE hMod = g_hModule;
        if (!hMod)
            hMod = GetModuleHandleW(nullptr);

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
        if (dirLen + sufLen + 1 > bufChars) return 0;
        memcpy(buf, path, dirLen);
        memcpy(buf + dirLen, suffix, sufLen + 1); // include null terminator
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

            // Trim value and copy out.
            const char* vBegin = eq + 1;
            const char* vEnd = lineEnd;
            while (vBegin < vEnd && (*vBegin == ' ' || *vBegin == '\t')) ++vBegin;
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
        return (strcmp(v, "1") == 0 || EqI(v, "true") || EqI(v, "yes"));
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
        if (outSize) *outSize = got;
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
            g_logEnabled = ParseBool(val, true);
            Logger::g_logWriteEnabled.store(g_logEnabled, std::memory_order_release);
        }

        if (ReadIniValue(content, "PillarSuppress", "Value", val, sizeof(val)))
            g_pillarSuppressEnabled = ParseBool(val, true);

        if (ReadIniValue(content, "PickupSuppress", "Value", val, sizeof(val)))
            g_pickupSuppressEnabled = ParseBool(val, true);

        if (ReadIniValue(content, "PillarFilter", "Value", val, sizeof(val)) ||
            ReadIniValue(content, "PillarFilter", "Enabled", val, sizeof(val)))
            g_pillarFilterEnabled = ParseBool(val, true);

        LOG("Config", "Reload: PillarFilter=%d Log=%d PillarSuppress=%d PickupSuppress=%d",
            (int)g_pillarFilterEnabled, (int)g_logEnabled,
            (int)g_pillarSuppressEnabled, (int)g_pickupSuppressEnabled);

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

    void StartHotReload()
    {
        Reload();
        LOG_MSG("Config", "Lazy hot-reload enabled");
    }

    void StopHotReload()
    {
    }
}