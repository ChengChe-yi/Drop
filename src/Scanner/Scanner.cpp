#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Scanner.h"
#include <cstdint>
#include <cstdlib>
#include <psapi.h>

namespace Scanner
{
    static uintptr_t GetMainModBase()
    {
        static uintptr_t base = 0;
        if (base == 0)
            base = (uintptr_t)GetModuleHandleW(nullptr);
        return base;
    }

    static size_t GetMainModSize()
    {
        static size_t size = 0;
        if (size == 0)
        {
            MODULEINFO info = {};
            if (GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &info, sizeof(info)))
                size = info.SizeOfImage;
        }
        return size;
    }

    // Pattern element: byte value + valid flag (false for '?' wildcard).
    static constexpr size_t kMaxPatternBytes = 64;

    // Parse a hex pattern like "48 8B 05 ?? ?? ?? 7F" into patBytes/patMask.
    // Returns the number of bytes parsed, or 0 on failure.
    static size_t ParsePattern(const char* pattern,
                               uint8_t* patBytes, uint8_t* patMask, size_t maxBytes)
    {
        if (!pattern) return 0;

        size_t out = 0;
        char hex[3] = {};
        int hexIdx = 0;

        for (size_t i = 0; pattern[i] && out < maxBytes; ++i)
        {
            char c = pattern[i];
            if (c == ' ' || c == '\t') continue;

            if (c == '?')
            {
                patBytes[out] = 0;
                patMask[out]  = 0;
                ++out;
                if (pattern[i + 1] == '?') ++i;
                continue;
            }

            hex[hexIdx++] = c;
            if (hexIdx == 2)
            {
                hex[2] = 0;
                unsigned long v = strtoul(hex, nullptr, 16);
                patBytes[out] = (uint8_t)v;
                patMask[out]  = 1;
                ++out;
                hexIdx = 0;
            }
        }
        return out;
    }

    uintptr_t ScanMainMod(const char* signature)
    {
        uintptr_t base = GetMainModBase();
        size_t size = GetMainModSize();
        if (!base || !size) return 0;
        return ScanRange(base, size, signature);
    }

    static uintptr_t ScanRaw(const uint8_t* data, size_t size,
                             const uint8_t* pattern, const uint8_t* mask, size_t patternLen)
    {
        __try {
            for (size_t i = 0; i <= size - patternLen; ++i)
            {
                bool match = true;
                for (size_t j = 0; j < patternLen; ++j)
                {
                    if (mask[j] && data[i + j] != pattern[j])
                    { match = false; break; }
                }
                if (match) return (uintptr_t)(data + i);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    uintptr_t ScanRange(uintptr_t start, size_t size, const char* signature)
    {
        if (!start || !size || !signature || !*signature) return 0;

        uint8_t patBytes[kMaxPatternBytes];
        uint8_t patMask[kMaxPatternBytes];
        size_t patternLen = ParsePattern(signature, patBytes, patMask, kMaxPatternBytes);
        if (patternLen == 0) return 0;

        return ScanRaw((const uint8_t*)start, size, patBytes, patMask, patternLen);
    }

    uintptr_t ResolveRelative(uintptr_t instruction, int offset, int instrSize)
    {
        if (!instruction) return 0;
        int32_t rel = *(int32_t*)(instruction + offset);
        return instruction + instrSize + rel;
    }
}