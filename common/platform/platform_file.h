#pragma once

#include <common/types.h>

namespace cc
{
    struct stat_t
    {
        uint32_t st_dev;
        uint16_t st_ino;
        uint16_t st_mode;
        int16_t  st_nlink;
        int16_t  st_uid;
        int16_t  st_gid;
        int16_t  pad0;
        uint32_t st_rdev;
        uint32_t pad1;
        int64_t  st_size;
        int64_t  st_atime;
        int64_t  st_mtime;
        int64_t  st_ctime;
    };

    // returns 0 if the file exists, -1 otherwise.
    // stat_t may be null (unlike POSIX)
    int stat(char const* path, stat_t*);
} // namespace cc
