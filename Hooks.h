#pragma once

namespace Hooks
{
    bool Init();
    void Uninit();
}

extern thread_local wchar_t g_itemName[64];
