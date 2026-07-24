#pragma once

namespace Whitelist
{
    // Load/reload whitelist from Whitelist.ini
    void Load();

    // Check if an item name is in the pickup whitelist
    bool IsPickupAllowed(const char* name);

    // Poll for file changes (call from PD_Thunk after Config::Tick())
    void Tick();
}
