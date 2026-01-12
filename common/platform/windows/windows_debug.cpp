#include <common/debug.h>

#include <common/platform/windows.h>

namespace cc
{
    bool is_debugging()
    {
        return !!::IsDebuggerPresent();
    }
} // namespace cc
