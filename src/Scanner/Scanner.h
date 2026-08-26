#pragma once
#include <cstdint>

namespace Scanner
{
    uintptr_t ScanMainMod(const char* signature);
    uintptr_t ScanRange(uintptr_t start, size_t size, const char* signature);
    uintptr_t ResolveRelative(uintptr_t instruction, int offset = 1, int instrSize = 5);
}