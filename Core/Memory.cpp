#include "pch.h"
#include "Memory.h"
#include <psapi.h>

namespace Memory
{

 

    uintptr_t GetMainModuleBase()
    {
        static uintptr_t base = 0;
        if (base == 0)
        {
            base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        }
        return base;
    }

    size_t GetMainModuleSize()
    {
        static size_t size = 0;
        if (size == 0)
        {
            MODULEINFO info = {};
            if (GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(nullptr), &info, sizeof(info)))
            {
                size = info.SizeOfImage;
            }
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
            if (c == ' ' || c == '\t')
                continue;

            if (c == '?')
            {
                result.emplace_back(0x00, false);
                if (i + 1 < pattern.size() && pattern[i + 1] == '?')
                    ++i;
                continue;
            }

            hex += c;
            if (hex.size() == 2)
            {
                uint8_t byte = static_cast<uint8_t>(std::stoi(hex, nullptr, 16));
                result.emplace_back(byte, true);
                hex.clear();
            }
        }

        return result;
    }

    // Raw memory scanner — SEH-safe (no C++ objects, POD types only)
    static ScanResult ScanPatternRaw(uintptr_t start, size_t size,
                                     const uint8_t* patBytes, const uint8_t* patMask, size_t patternLen)
    {
        ScanResult result;
        __try {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(start);
            for (size_t i = 0; i <= size - patternLen; ++i)
            {
                bool match = true;
                for (size_t j = 0; j < patternLen; ++j)
                {
                    if (patMask[j] && data[i + j] != patBytes[j])
                    { match = false; break; }
                }
                if (match)
                {
                    result.address = start + i;
                    result.found   = true;
                    return result;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return result;
    }

    ScanResult ScanPattern(uintptr_t start, size_t size, const std::string& pattern)
    {
        if (start == 0 || size == 0 || pattern.empty())
            return {};

        auto parsed = ParsePattern(pattern);
        if (parsed.empty())
            return {};

        // Convert to raw arrays (POD) for SEH-safe scanning
        std::vector<uint8_t> patBytes(parsed.size());
        std::vector<uint8_t> patMask(parsed.size());
        for (size_t i = 0; i < parsed.size(); i++)
        {
            patBytes[i] = parsed[i].first;
            patMask[i] = parsed[i].second ? 1 : 0;
        }

        return ScanPatternRaw(start, size, patBytes.data(), patMask.data(), parsed.size());
    }

    ScanResult ScanMainModule(const std::string& pattern)
    {
        uintptr_t base = GetMainModuleBase();
        size_t    size = GetMainModuleSize();

        if (base == 0 || size == 0)
            return {};

        return ScanPattern(base, size, pattern);
    }


    uintptr_t ResolveRelative(uintptr_t instructionAddr, int instructionLen)
    {
        if (instructionAddr == 0)
            return 0;


        int32_t offset = *reinterpret_cast<int32_t*>(instructionAddr + 1);
        return instructionAddr + instructionLen + offset;
    }

} 
