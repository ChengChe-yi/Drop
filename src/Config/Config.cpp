#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Config.h"
#include "Watcher.h"
#include "Logger.h"
#include "Whitelist.h"
#include <cstring>

namespace Config
{
    // __ImageBase 的地址即本 DLL 的 HMODULE，用于把配置锚定在 DLL 侧而非宿主 EXE。
    extern "C" IMAGE_DOS_HEADER __ImageBase;

    std::atomic<bool> g_logEnabled{true};
    std::atomic<bool> g_pickupSuppressEnabled{true};

    static char      g_cachedDir[MAX_PATH];
    static bool      g_cachedDirReady = false;

    size_t GetModuleDir(char* buf, size_t bufChars)
    {
        if (!buf || bufChars == 0) return 0;
        buf[0] = 0;

        if (g_cachedDirReady) {
            size_t len = strlen(g_cachedDir);
            if (len + 1 > bufChars) return 0;
            memcpy(buf, g_cachedDir, len + 1);
            return len;
        }

        HMODULE hMod = reinterpret_cast<HMODULE>(&__ImageBase);
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(hMod, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return 0;

        // 截到最后一个路径分隔符（保留尾部反斜杠）。
        char* lastSlash = nullptr;
        for (char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') lastSlash = p;
        if (!lastSlash) return 0;

        size_t dirLen = (size_t)(lastSlash - path) + 1;
        if (dirLen + 1 > sizeof(g_cachedDir)) return 0;
        memcpy(g_cachedDir, path, dirLen);
        g_cachedDir[dirLen] = 0;
        g_cachedDirReady = true;

        if (dirLen + 1 > bufChars) return 0;
        memcpy(buf, g_cachedDir, dirLen + 1);
        return dirLen;
    }

    size_t GetConfigPath(char* buf, size_t bufChars)
    {
        char dir[MAX_PATH];
        size_t dirLen = GetModuleDir(dir, sizeof(dir));
        if (dirLen == 0) return 0;

        const char* suffix = "Config.ini";
        size_t sufLen = strlen(suffix);
        if (dirLen + sufLen + 1 > bufChars) return 0;
        memcpy(buf, dir, dirLen);
        memcpy(buf + dirLen, suffix, sufLen + 1);
        return dirLen + sufLen;
    }

    // 在 content 的 [section] 块内查找 key 的值（去空白、截断行内 ;/# 注释）。
    static bool ReadIniValue(const char* content,
                             const char* section,
                             const char* key,
                             char* buf,
                             size_t bufChars)
    {
        if (!content || !section || !key || !buf || bufChars == 0) return false;
        buf[0] = 0;

        char header[128];
        size_t secLen = strlen(section);
        if (secLen + 2 >= sizeof(header)) return false;
        header[0] = '[';
        memcpy(header + 1, section, secLen);
        header[secLen + 1] = ']';
        header[secLen + 2] = 0;

        const char* p = strstr(content, header);
        if (!p) return false;

        // 找到 section 结束位置（下一个 "[" 行头或 EOF）。
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

        const char* pos = sectionStart + strlen(header);
        size_t keyLen = strlen(key);
        while (pos < sectionEnd) {
            while (pos < sectionEnd && (*pos == '\r' || *pos == '\n'))
                ++pos;
            if (pos >= sectionEnd) break;

            const char* lineEnd = pos;
            while (lineEnd < sectionEnd && *lineEnd != '\r' && *lineEnd != '\n')
                ++lineEnd;

            if (*pos == ';' || *pos == '#') {
                pos = lineEnd + 1;
                continue;
            }

            const char* eq = nullptr;
            for (const char* q = pos; q < lineEnd; ++q)
                if (*q == '=') { eq = q; break; }
            if (!eq) {
                pos = lineEnd + 1;
                continue;
            }

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

            const char* vBegin = eq + 1;
            const char* vEnd = lineEnd;
            while (vBegin < vEnd && (*vBegin == ' ' || *vBegin == '\t')) ++vBegin;
            while (vEnd > vBegin && (*(vEnd - 1) == ' ' || *(vEnd - 1) == '\t')) --vEnd;
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

    // ASCII 范围内的不区分大小写比较，避免依赖 CRT 的 _stricmp。
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

    // 整读文件到堆缓冲，调用方 delete[] 释放；失败返回 nullptr。
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

        // 跳过 UTF-8 BOM，保证 strstr 能匹配 "[section]"；前移后 memmove 归位，
        // delete[] 仍对准原始分配。
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

        memmove(buf, content, contentLen);
        buf[contentLen] = 0;
        if (outSize) *outSize = contentLen;
        return buf;
    }

    // 在 watcher 线程执行：纯读文件 + 原子写，无锁、不触碰 loader。
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

    void StartHotReload()
    {
        // 初始加载在 worker 线程完成；watcher 随后播种 mtime，首次检查不会误触发。
        Reload();
        Whitelist::Load();

        char dir[MAX_PATH];
        if (GetModuleDir(dir, sizeof(dir)) == 0) {
            LOG_MSG("Config", "Cannot resolve module dir, hot reload disabled");
            return;
        }

        if (Watcher::Start(dir, &Reload, &Whitelist::Load))
            LOG_MSG("Config", "Hot reload enabled (event-driven, 200ms debounce)");
        else
            LOG_MSG("Config", "Watcher failed to start, hot reload disabled");
    }

    void StopHotReload()
    {
        Watcher::Stop();
    }
}
