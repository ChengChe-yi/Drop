#pragma once

namespace Whitelist
{

    void Load();

    bool IsPickupAllowed(const char* name);

    void Tick();
}
