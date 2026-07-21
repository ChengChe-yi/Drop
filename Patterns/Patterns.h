#pragma once
#include <cstdint>
#include <string>

namespace Offsets
{

    inline std::string GetActiveOffset;  
    inline std::string GetNameOffset;     
    inline std::string GetTextOffset;  


    namespace RVA
    {
        inline constexpr uintptr_t SetActive    = 0x1131C50;

        inline constexpr uintptr_t GetName      = 0x1117FF0;

        inline constexpr uintptr_t PickupDataAdd = 0x9459DA0;

        inline constexpr uintptr_t B81160_RVA     = 0x11B81160;
    }

    void InitOffsets();
}
