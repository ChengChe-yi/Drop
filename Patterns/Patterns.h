#pragma once
#include <cstdint>

// === Signature library (IDA-verified, unique per function) ==================
//
//  B81160:       56 57 55 53 48 83 EC 28 4C 89 CF 4C 89 C6 89 D5
//                48 89 CB 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ??
//                80 3D ?? ?? ?? ?? 00 0F 84 ?? ?? ?? ??
//                48 85 F6 0F 84 ?? ?? ?? ?? 48 89 F1 89 EA E8
//
//  PickupData:   41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 68
//                48 89 D7 49 89 CE 80 3D ?? ?? ?? ?? 00 0F 85
//
//  GetName:      40 53 48 81 EC ?? ?? ?? ?? 48 8B D9 48 85 C9
//                0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ??
//                48 85 C0 0F 84
// ============================================================================

namespace Offsets
{
    namespace RVA
    {
        inline constexpr uintptr_t SetActive    = 0x1131C50;
        inline constexpr uintptr_t GetName      = 0x1117FF0;
        inline constexpr uintptr_t PickupDataAdd = 0x9459DA0;
        inline constexpr uintptr_t B81160_RVA   = 0x11B81160;
    }
}
