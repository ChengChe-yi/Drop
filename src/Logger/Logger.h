#pragma once
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>
#include <atomic>

// 轻量文件日志：写入 DLL 同目录 Drop.log（UTF-8，自动补 BOM）。
// 句柄缓存：写入期间保持文件打开，空闲 1s 后由线程池定时器关闭；
// 每行 fflush 落盘，崩溃时已写内容不丢。
// 状态全部收在 Logger.cpp 单一编译单元，避免 inline 变量的 ODR 问题。

namespace Logger
{
    // 总开关，Reload() 后镜像 Config.ini → [Log] Value。
    extern std::atomic<bool> g_logWriteEnabled;

    // 初始化日志路径；可重复调用，仅首次生效。
    void InitLogFile(HMODULE hModule);

    // 追加一行（调用方已完成格式化）。
    void WriteLog(const char* text);

    // 关闭日志：取消空闲定时器并释放缓存的句柄；仅动态卸载时调用一次。
    void CloseLog();
}

// LOG(tag, fmt, ...) —— printf 风格，自动加 [时:分:秒.毫秒][tag] 前缀。
// LOG_MSG(tag, msg)  —— 固定字符串版本。
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
