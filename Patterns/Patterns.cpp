#include "pch.h"
#include "Patterns.h"

namespace Offsets
{
    void InitOffsets()
    {
        // Current version offsets
        GetActiveOffset = "1131C50";  // SetActive_Wrapper (not the real SetActive)
        GetNameOffset   = "1117FF0";  // GetName
        GetTextOffset   = "";         // GetText — scan if needed
    }
}
