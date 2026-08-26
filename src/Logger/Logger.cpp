#include "Logger.h"
#include <cstring>

namespace Logger
{
    std::atomic<bool> g_logWriteEnabled{true};

    static wchar_t g_logPath[MAX_PATH] = {};
    static bool    g_logPathReady = false;
    static SRWLOCK g_logLock = SRWLOCK_INIT;

    void InitLogFile(HMODULE hModule)
    {
        if (g_logPathReady) return;

        wchar_t modulePath[MAX_PATH] = {};
        if (!hModule || GetModuleFileNameW(hModule, modulePath, MAX_PATH) == 0)
            GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

        wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
        if (lastSlash)
            wcscpy_s(lastSlash + 1, MAX_PATH - (lastSlash - modulePath + 1), L"Drop.log");
        else
            wcscpy_s(modulePath, L"Drop.log");

        wcscpy_s(g_logPath, modulePath);
        g_logPathReady = (g_logPath[0] != 0);
    }

    void WriteLog(const char* text)
    {
        if (!g_logWriteEnabled.load(std::memory_order_acquire) || !g_logPathReady)
            return;

        AcquireSRWLockExclusive(&g_logLock);
        FILE* f = nullptr;
        _wfopen_s(&f, g_logPath, L"ab");
        if (f) {
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

    void CloseLog()
    {
    }
}