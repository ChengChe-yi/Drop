#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Ini.h"
#include <cstring>

namespace
{
    bool IsSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    // 拷贝 [start, end) 去首尾空白后写入 out，返回拷贝字节数。
    size_t CopyTrimmed(const char* start, const char* end,
                       char* out, size_t outChars)
    {
        if (outChars == 0) return 0;
        while (start < end && IsSpace(*start)) ++start;
        while (end > start && IsSpace(*(end - 1))) --end;
        size_t len = (size_t)(end - start);
        if (len >= outChars) len = outChars - 1;
        memcpy(out, start, len);
        out[len] = 0;
        return len;
    }

    // 在 content 中查找 "[section]" 头，返回指向 '[' 的指针或 nullptr。
    const char* FindSectionHeader(const char* content, const char* section)
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
    const char* FindSectionEnd(const char* headerStart)
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

    // ASCII 范围内的不区分大小写比较，避免依赖 CRT 的 _stricmp。
    bool EqI(const char* a, const char* b)
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
}

char* Ini::LoadFile(const char* path, size_t* outSize, size_t maxBytes)
{
    if (outSize) *outSize = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return nullptr;

    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 ||
        (maxBytes != 0 && (size_t)size > maxBytes)) {
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

    // 跳过 UTF-8 BOM，保证 strstr 能匹配 "[Section]"；前移后 memmove 归位，
    // delete[] 仍对准原始分配。
    size_t skip = 0;
    if (got >= 3 &&
        (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) {
        skip = 3;
    }

    if (skip) {
        memmove(buf, buf + skip, got - (DWORD)skip);
        got -= (DWORD)skip;
    }
    buf[got] = 0;

    if (outSize) *outSize = got;
    return buf;
}

bool Ini::GetValue(const char* content, const char* section, const char* key,
                   char* out, size_t outChars)
{
    if (!content || !section || !key || !out || outChars == 0) return false;
    out[0] = 0;

    const char* sectionStart = FindSectionHeader(content, section);
    if (!sectionStart) return false;

    // 找到 section 结束位置（下一个 "[" 行头之前，或 EOF）。
    const char* searchPos = sectionStart + strlen(section);
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

    const char* pos = sectionStart + strlen(section);
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

        if ((size_t)(kEnd - kBegin) != keyLen ||
            memcmp(kBegin, key, keyLen) != 0) {
            pos = lineEnd + 1;
            continue;
        }

        const char* vBegin = eq + 1;
        const char* vEnd = lineEnd;
        for (const char* c = vBegin; c < vEnd; ++c) {
            if (*c == ';' || *c == '#') { vEnd = c; break; }
        }
        while (vBegin < vEnd && (*vBegin == ' ' || *vBegin == '\t')) ++vBegin;
        while (vEnd > vBegin && (*(vEnd - 1) == ' ' || *(vEnd - 1) == '\t')) --vEnd;

        size_t valLen = (size_t)(vEnd - vBegin);
        if (valLen >= outChars) valLen = outChars - 1;
        memcpy(out, vBegin, valLen);
        out[valLen] = 0;
        return true;
    }
    return false;
}

bool Ini::ParseBool(const char* value, bool defaultVal)
{
    if (!value || !*value) return defaultVal;
    if (EqI(value, "1") || EqI(value, "true") || EqI(value, "yes") || EqI(value, "on"))  return true;
    if (EqI(value, "0") || EqI(value, "false") || EqI(value, "no")  || EqI(value, "off")) return false;
    return defaultVal;
}

bool Ini::ForEachEntry(const char* content, const char* section,
                       EntryCallback cb, void* ctx)
{
    if (!content || !section || !cb) return false;

    const char* hdr = FindSectionHeader(content, section);
    if (!hdr) return false;

    const char* sectionEnd = FindSectionEnd(hdr);
    const char* pos = strchr(hdr, '\n');
    if (!pos) return false;
    ++pos;

    while (pos < sectionEnd) {
        const char* nl = pos;
        while (nl < sectionEnd && *nl != '\n') ++nl;

        char trimmed[256];
        CopyTrimmed(pos, nl, trimmed, sizeof(trimmed));

        // 空行与注释行不回调。
        if (trimmed[0] && trimmed[0] != ';' && trimmed[0] != '#') {
            if (!cb(trimmed, ctx)) return true;   // 回调要求提前终止。
        }

        pos = nl + 1;
    }
    return true;
}
