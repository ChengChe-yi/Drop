#include "pch.h"
#include "Scanner.h"
#include <vector>
#include <psapi.h>

namespace Scanner
{
    static uintptr_t GetMainModBase()
    {
        static uintptr_t base = 0;
        if (base == 0)
            base = (uintptr_t)GetModuleHandleW(L"YuanShen.exe");
        return base;
    }

    static size_t GetMainModSize()
    {
        static size_t size = 0;
        if (size == 0)
        {
            MODULEINFO info = {};
            if (GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(L"YuanShen.exe"), &info, sizeof(info)))
                size = info.SizeOfImage;
        }
        return size;
    }

    static std::vector<std::pair<uint8_t, bool>> ParsePattern(const std::string& pattern)
    {
        std::vector<std::pair<uint8_t, bool>> result;
        std::string hex;
        for (size_t i = 0; i < pattern.size(); ++i)
        {
            char c = pattern[i];
            if (c == ' ' || c == '\t') continue;
            if (c == '?' || c == '?')
            {
                result.emplace_back(0x00, false);
                if (i + 1 < pattern.size() && (pattern[i+1] == '?' || pattern[i+1] == '?'))
                    ++i;
                continue;
            }
            hex += c;
            if (hex.size() == 2)
            {
                result.emplace_back((uint8_t)std::stoul(hex, nullptr, 16), true);
                hex.clear();
            }
        }
        return result;
    }

    uintptr_t ScanMainMod(const std::string& signature)
    {
        uintptr_t base = GetMainModBase();
        size_t size = GetMainModSize();
        if (!base || !size) return 0;
        return ScanRange(base, size, signature);
    }

    static uintptr_t ScanRaw(const uint8_t* data, size_t size, const uint8_t* pattern, const uint8_t* mask, size_t patternLen)
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

    uintptr_t ScanRange(uintptr_t start, size_t size, const std::string& signature)
    {
        if (!start || !size || signature.empty()) return 0;
        auto parsed = ParsePattern(signature);
        if (parsed.empty()) return 0;

        // Extract pattern and mask to C arrays for SEH-safe scanning
        std::vector<uint8_t> patBytes(parsed.size());
        std::vector<uint8_t> patMask(parsed.size());
        for (size_t i = 0; i < parsed.size(); i++)
        {
            patBytes[i] = parsed[i].first;
            patMask[i] = parsed[i].second ? 1 : 0;
        }

        return ScanRaw((const uint8_t*)start, size, patBytes.data(), patMask.data(), parsed.size());
    }

    uintptr_t ResolveRelative(uintptr_t instruction, int offset, int instrSize)
    {
        if (!instruction) return 0;
        int32_t rel = *(int32_t*)(instruction + offset);
        return instruction + instrSize + rel;
    }
}
