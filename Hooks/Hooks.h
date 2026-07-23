#pragma once
#include <cstdint>

namespace Hooks
{
    bool Init();
    void Uninit();
}

namespace Int3Hook
{
    bool InitVEH();
    void ShutdownVEH();
    bool Install(uint8_t* target, void* thunk);
    void Uninstall(uint8_t* target);
    void ReArm(uint8_t* target);
}
