#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Whitelist.h"
#include "Config.h"
#include "Logger.h"
#include <cstring>

namespace Whitelist
{
    // Fixed-size whitelist. Each entry is a C string of up to kEntryLen chars.
    static constexpr size_t kMaxEntries = 256;
    static constexpr size_t kEntryLen   = 128;

    static char  g_pickupWhitelist[kMaxEntries][kEntryLen];
    static size_t g_pickupCount = 0;
    static char  s_filePath[MAX_PATH];
    static bool  s_filePathReady = false;
    static ULONGLONG s_lastCheck = 0;
    static ULONGLONG s_lastWrite = 0;

    static const char* GetFilePath()
    {
        if (s_filePathReady) return s_filePath;

        char configPath[MAX_PATH];
        if (Config::GetConfigPath(configPath, sizeof(configPath)) == 0) {
            s_filePath[0] = 0;
            s_filePathReady = true;
            return s_filePath;
        }

        // Strip to directory portion.
        char* lastSlash = nullptr;
        for (char* p = configPath; *p; ++p)
            if (*p == '\\' || *p == '/') lastSlash = p;
        if (lastSlash)
            lastSlash[1] = 0;
        else
            configPath[0] = 0;

        strcpy_s(s_filePath, configPath);
        strcat_s(s_filePath, "Whitelist.ini");
        s_filePathReady = true;
        return s_filePath;
    }

    // Copy up to outChars-1 bytes from `start` (delimited by `end`) into `out`.
    // Strips leading/trailing whitespace, returns number of bytes copied.
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

    // Find "[section]" header inside `content`. Returns pointer to its '[' or nullptr.
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

    // Locate the end of the section that starts at `headerStart` (points at '[').
    // Returns the pointer to the '\n' just before the next "[" header, or to
    // the null terminator if there is no next section.
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
                             char (*out)[kEntryLen],
                             size_t outMax,
                             size_t* outCount)
    {
        *outCount = 0;
        const char* hdr = FindSection(content, section);
        if (!hdr) return;

        const char* sectionEnd = FindSectionEnd(hdr);
        const char* pos = strchr(hdr, '\n');
        if (!pos) return;
        ++pos;

        while (pos < sectionEnd && *outCount < outMax) {
            const char* nl = pos;
            while (nl < sectionEnd && *nl != '\n') ++nl;

            char trimmed[kEntryLen];
            CopyTrimmed(pos, nl, trimmed, sizeof(trimmed));

            if (trimmed[0] && trimmed[0] != ';' && trimmed[0] != '#') {
                memcpy(out[*outCount], trimmed, sizeof(trimmed));
                ++(*outCount);
            }

            pos = nl + 1;
        }
    }

    void Load()
    {
        const char* path = GetFilePath();

        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            g_pickupCount = 0;
            return;
        }

        DWORD size = GetFileSize(h, NULL);
        if (size == INVALID_FILE_SIZE || size == 0) {
            CloseHandle(h);
            g_pickupCount = 0;
            return;
        }

        // Read into a heap buffer with one extra byte for the null terminator.
        // Cap at a reasonable maximum so a runaway file can't allocate gigabytes.
        char stackBuf[8192];
        char* content = stackBuf;
        char* heapBuf = nullptr;
        if (size + 1 > sizeof(stackBuf)) {
            if (size + 1 > 1 * 1024 * 1024) { // 1 MiB hard cap
                CloseHandle(h);
                g_pickupCount = 0;
                return;
            }
            heapBuf = new char[size + 1];
            content = heapBuf;
        }

        DWORD got = 0;
        BOOL ok2 = ReadFile(h, content, size, &got, NULL);
        CloseHandle(h);
        if (!ok2 || got != size) {
            delete[] heapBuf;
            g_pickupCount = 0;
            return;
        }
        content[got] = 0;

        // Skip a leading UTF-8 BOM (EF BB BF) if present, so that an editor-
        // saved Whitelist.ini still matches its "[PickupSuppress]" header.
        if (got >= 3 &&
            (unsigned char)content[0] == 0xEF &&
            (unsigned char)content[1] == 0xBB &&
            (unsigned char)content[2] == 0xBF)
        {
            content += 3;
            got -= 3;
        }

        ParseSection(content, "PickupSuppress",
                     g_pickupWhitelist, kMaxEntries, &g_pickupCount);

        LOG("Whitelist", "Loaded %zu items from %s", g_pickupCount, path);

        delete[] heapBuf;
    }

    bool IsPickupAllowed(const char* name)
    {
        if (!name || !*name) return false;
        for (size_t i = 0; i < g_pickupCount; ++i) {
            if (strcmp(g_pickupWhitelist[i], name) == 0)
                return true;
        }
        return false;
    }

    void Tick()
    {
        ULONGLONG now = GetTickCount64();
        if (now - s_lastCheck < 1000) return;
        s_lastCheck = now;

        const char* path = GetFilePath();
        HANDLE h = CreateFileA(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            FILETIME ft;
            if (GetFileTime(h, NULL, NULL, &ft)) {
                ULONGLONG wt = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                if (wt != s_lastWrite) {
                    s_lastWrite = wt;
                    LOG_MSG("Whitelist", "File change detected, reloading...");
                    Load();
                }
            }
            CloseHandle(h);
        }
    }
}