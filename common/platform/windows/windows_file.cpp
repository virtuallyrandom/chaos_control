#include <common/platform/platform_file.h>
#include <common/platform/windows.h>

namespace cc
{
    int stat(char const* const path, stat_t* const result)
    {
        assert(nullptr != path);
        if (nullptr == path)
            return -1;

        struct _stat32i64 st {};
        int const err = ::_stat32i64(path, &st);
        if (0 == err && nullptr != result)
        {
            result->st_dev = st.st_dev;
            result->st_ino = st.st_ino;
            result->st_mode = st.st_mode;
            result->st_nlink = st.st_nlink;
            result->st_uid = st.st_uid;
            result->st_gid = st.st_gid;
            result->st_rdev = st.st_rdev;
            result->st_size = st.st_size;
            result->st_atime = st.st_atime;
            result->st_mtime = st.st_mtime;
            result->st_ctime = st.st_ctime;
        }
        return err;
    }
} // namespace cc
