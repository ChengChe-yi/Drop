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
        inline constexpr uintptr_t SetActive        = 0x1131C50;
        inline constexpr uintptr_t SetActiveUnwrap  = 0x1129D70;   
        inline constexpr uintptr_t GameObjectSetActive = 0x168BE30; 
        inline constexpr uintptr_t GetName          = 0x1117FF0;
        inline constexpr uintptr_t PickupDataAdd    = 0xF4944B0;   // IDA: sub_14F4944B0 (VA 0x14F4944B0, base 0x140000000 => RVA 0xF4944B0); 旧值 0x9459DA0 已失效
        inline constexpr uintptr_t B81160_RVA       = 0x11B81160;
        inline constexpr uintptr_t AddPickupOption  = 0xBF5F7D0;
        inline constexpr uintptr_t CreatePickupItem = 0xDAE3060;   
        inline constexpr uintptr_t ConsumePickupIcon= 0xD5D2070;   
        inline constexpr uintptr_t Case0UpdateItem  = 0xDAE3430;   
        inline constexpr uintptr_t Case1CreateItem  = 0xDAE3060;   
        inline constexpr uintptr_t FadeInItem       = 0xF4D8690;   
        inline constexpr uintptr_t CommitListItem   = 0xDAD1E70;   
    }

    inline constexpr const char* GetFrameCountPat =
        "E8 ? ? ? ? 85 C0 7E 0E E8 ? ? ? ? 0F 57 C0 F3 0F 2A C0 EB 08";


    inline constexpr const char* ChangeFOVPat =
        "40 53 48 83 EC 60 0F 29 74 24 ? 48 8B D9 0F 28 F1 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 48 8B C8";

    inline constexpr const char* SetActivePat =
        "E8 ? ? ? ? 48 8B 56 ? 48 85 D2 0F 84 ? ? ? ? 80 3D ? ? ? ? 0 0F 85 ? ? ? ? 48 89 D1 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 89 C1";


    inline constexpr const char* GetNamePat =
        "40 53 48 81 EC ?? ?? ?? ?? 48 8B D9 48 85 C9 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0 0F 84 ?? ?? ?? ?? 48 8B 10 48 8B C8 FF 52 ?? 48 85 C0 0F 85 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ??";
}
