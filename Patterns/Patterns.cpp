#include "pch.h"
#include "Patterns.h"

namespace Offsets
{
    void InitOffsets()
    {
        // Current version offsets (string form, for configurable scanning)
        GetActiveOffset = "1131C50";  // SetActive_Wrapper
        GetNameOffset   = "1117FF0";  // GetName
        GetTextOffset   = "";         // GetText — scan if needed
    }
}
