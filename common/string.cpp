#include <common/string.h>

namespace cc
{

    int strcasecmp(const char* a, const char* b)
    {
        return _stricmp(a, b);
    }

    int strncasecmp(const char* a, const char* b, size_t count)
    {
        return _strnicmp(a, b, count);
    }

} // namespace cc
