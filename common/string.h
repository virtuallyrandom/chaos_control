#pragma once

#include <cstdio>
#include <string>

namespace cc
{
    using std::string;

    using ::strlen;

    int strcasecmp(const char* a, const char* b);
    int strncasecmp(const char* a, const char* b, size_t count);
} // namespace cc
