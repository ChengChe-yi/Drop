#pragma once

namespace Whitelist
{

    void Load();

    // Records the current mtime of Whitelist.ini into the tick cache so the
    // next Tick() call doesn't fire a spurious "File change detected" reload
    // right after startup. Call this once after Load().
    void SeedMTime();

    bool IsPickupAllowed(const char* name);

    void Tick();
}
