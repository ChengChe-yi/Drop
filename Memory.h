#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <span>

namespace Memory
{
    
    uintptr_t GetMainModuleBase();


    size_t GetMainModuleSize();


    inline uintptr_t Offset(uintptr_t base, ptrdiff_t offset)
    {
        return base + offset;
    }


    inline uintptr_t Offset(ptrdiff_t offset)
    {
        return GetMainModuleBase() + offset;
    }


    struct ScanResult
    {
        uintptr_t address = 0;
        bool      found   = false;

        explicit operator bool() const { return found; }
        uintptr_t operator*() const    { return address; }
    };


    ScanResult ScanPattern(uintptr_t start, size_t size, const std::string& pattern);

    ScanResult ScanMainModule(const std::string& pattern);

    uintptr_t ResolveRelative(uintptr_t instructionAddr, int instructionLen = 5);

} 
