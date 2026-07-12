#pragma once
#include <cstdint>
#include <string>

namespace Offsets
{
    // Offset strings (hex) — read from here, fallback to signature scan if empty
    inline std::string GetActiveOffset;  // SetActive offset (empty = scan)
    inline std::string GetNameOffset;    // GetName offset
    inline std::string GetTextOffset;    // UI Text component offset

    void InitOffsets();
}
