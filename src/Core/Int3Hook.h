#pragma once
#include <cstdint>

namespace Int3Hook
{
    bool Install(void* target, void* handler);
    void Uninstall(void* target);
    void ReArm(void* target);
}
