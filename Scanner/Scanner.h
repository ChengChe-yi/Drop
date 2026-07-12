#pragma once
#include <cstdint>
#include <string>

namespace Scanner
{
    /// Scan main module memory for a byte pattern
    /// Pattern format: "48 8B ?? ?? ?? ?? FF E0" (? = wildcard)
    uintptr_t ScanMainMod(const std::string& signature);

    /// Scan a specific memory region for a byte pattern
    uintptr_t ScanRange(uintptr_t start, size_t size, const std::string& signature);

    /// Resolve RIP-relative call/jump target
    uintptr_t ResolveRelative(uintptr_t instruction, int offset = 1, int instrSize = 5);
}
