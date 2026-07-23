#pragma once
#include <cstdio>
#include <windows.h>
#include <cstring>

// ============================================================================
// Logger — writes to a file in the DLL's directory
// Opens the file on each write, closes immediately after (no persistent handle).
// Thread-safe via SRWLOCK.
// ============================================================================

// Global toggle — set by Config::Reload() from Config.ini [Log]
#include <atomic>
#include "XorStr.h"
inline std::atomic<bool> g_logWriteEnabled{true};

inline wchar_t g_logPath[MAX_PATH] = {};
inline bool    g_logPathReady = false;
inline SRWLOCK g_logLock = SRWLOCK_INIT;

static inline void InitLogFile(HMODULE hModule) {
    if (g_logPathReady) return;

    wchar_t modulePath[MAX_PATH] = {};
    // Try with provided module handle first (may fail for manual-mapped DLLs)
    if (!hModule || GetModuleFileNameW(hModule, modulePath, MAX_PATH) == 0)
    {
        // Fallback: use the game exe path
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    }

    wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
    if (lastSlash)
        wcscpy_s(lastSlash + 1, MAX_PATH - (lastSlash - modulePath + 1), L"Drop.log");
    else
        wcscpy_s(modulePath, L"Drop.log");

    wcscpy_s(g_logPath, modulePath);
    g_logPathReady = (g_logPath[0] != 0);
}

static inline void WriteLog(const char* text) {
    if (!g_logWriteEnabled || !g_logPathReady) return;

    AcquireSRWLockExclusive(&g_logLock);
    FILE* f = nullptr;
    _wfopen_s(&f, g_logPath, L"ab");
    if (f) {
        // Write UTF-8 BOM if file is empty
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0) {
            const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            fwrite(bom, 1, 3, f);
        }
        fwrite(text, 1, strlen(text), f);
        fclose(f);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

static inline void CloseLog() {
    // Nothing to close — handles are opened/closed per write
}

// LOG macro (file only, no OutputDebugString) — strings auto-encrypted via XSTR
#define LOG(tag, fmt, ...)                                                          \
    do {                                                                            \
        SYSTEMTIME _st;                                                             \
        GetLocalTime(&_st);                                                         \
        char _fmt[1024];                                                            \
        sprintf_s(_fmt, sizeof(_fmt), "[%%02d:%%02d:%%02d.%%03d][%s] %s\n",         \
            XSTR(tag), XSTR(fmt));                                                   \
        char _buf[1024];                                                            \
        int _len = sprintf_s(_buf, sizeof(_buf), _fmt,                              \
            _st.wHour, _st.wMinute, _st.wSecond, _st.wMilliseconds,                 \
            __VA_ARGS__);                                                           \
        if (_len > 0) WriteLog(_buf);                                               \
    } while (0)

#define LOG_MSG(tag, msg)                                                           \
    do {                                                                            \
        SYSTEMTIME _st;                                                             \
        GetLocalTime(&_st);                                                         \
        char _fmt[1024];                                                            \
        sprintf_s(_fmt, sizeof(_fmt), "[%%02d:%%02d:%%02d.%%03d][%s] %s\n",         \
            XSTR(tag), XSTR(msg));                                                   \
        char _buf[1024];                                                            \
        int _len = sprintf_s(_buf, sizeof(_buf), _fmt,                              \
            _st.wHour, _st.wMinute, _st.wSecond, _st.wMilliseconds);                \
        if (_len > 0) WriteLog(_buf);                                               \
    } while (0)
