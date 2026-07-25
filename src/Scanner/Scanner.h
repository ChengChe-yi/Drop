#pragma once
#include <cstdint>
#include <string>

namespace Scanner
{

    uintptr_t ScanMainMod(const std::string& signature);


    uintptr_t ScanRange(uintptr_t start, size_t size, const std::string& signature);


    uintptr_t ResolveRelative(uintptr_t instruction, int offset = 1, int instrSize = 5);
}
