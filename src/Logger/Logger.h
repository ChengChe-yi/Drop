#pragma once
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>
#include <atomic>

// ----------------------------------------------------------------------------
// Logger — thin file logger used by LOG() / LOG_MSG() macros.
//
// All state lives in Logger.cpp (single TU), so we use `extern` instead of
// C++17 `inline` variables. This avoids the ODR-with-mutable-state trap that
// existed in the previous header-only version where `g_logWriteEnabled` and
// `g_logLock` were `inline` and could be written from any TU without a clear
// owner.
// ----------------------------------------------------------------------------

namespace Logger
{
    // Master switch. Mirrors Config.ini → [Log] Value after Reload().
    extern std::atomic<bool> g_logWriteEnabled;

    // Initialize the log file path. Safe to call multiple times — only the
    // first call has any effect.
    void InitLogFile(HMODULE hModule);

    // Append a single line to Drop.log. Caller already formats the line.
    void WriteLog(const char* text);

    // Reserved for future buffer flushing. Currently a no-op.
    void CloseLog();
}

// Macro: LOG(tag, fmt, ...) — formatted printf-style log line.
// Macro: LOG_MSG(tag, msg)   — fixed-string log line.
#define LOG(tag, fmt, ...)                                                          \
    do {                                                                            \
        SYSTEMTIME _st;                                                             \
        GetLocalTime(&_st);                                                         \
        char _fmt[1024];                                                            \
        sprintf_s(_fmt, sizeof(_fmt), "[%%02d:%%02d:%%02d.%%03d][%s] %s\n",         \
            (tag), (fmt));                                                          \
        char _buf[1024];                                                            \
        int _len = sprintf_s(_buf, sizeof(_buf), _fmt,                              \
            _st.wHour, _st.wMinute, _st.wSecond, _st.wMilliseconds,                 \
            __VA_ARGS__);                                                           \
        if (_len > 0) ::Logger::WriteLog(_buf);                                     \
    } while (0)

#define LOG_MSG(tag, msg)                                                           \
    do {                                                                            \
        SYSTEMTIME _st;                                                             \
        GetLocalTime(&_st);                                                         \
        char _fmt[1024];                                                            \
        sprintf_s(_fmt, sizeof(_fmt), "[%%02d:%%02d:%%02d.%%03d][%s] %s\n",         \
            (tag), (msg));                                                          \
        char _buf[1024];                                                            \
        int _len = sprintf_s(_buf, sizeof(_buf), _fmt,                              \
            _st.wHour, _st.wMinute, _st.wSecond, _st.wMilliseconds);                \
        if (_len > 0) ::Logger::WriteLog(_buf);                                     \
    } while (0)