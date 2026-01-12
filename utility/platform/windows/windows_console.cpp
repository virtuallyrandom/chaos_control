#include <fcntl.h>
#include <io.h>

#include <utility/platform/console.h>

namespace cc::console_platform
{
    constexpr int kFile = 0;
    constexpr int kSock = 1;

    void redirect(FILE* const from, socket_type const to, int (&info)[2])
    {
        if (kInvalidSocket == to)
        {
            info[kFile] = -1;
            info[kSock] = -1;
            return;
        }

        info[kSock] = _open_osfhandle((intptr_t)to, _O_TEXT);
        if (info[kSock] != -1)
        {
            info[kFile] = _fileno(from);
            int dup2Result = _dup2(info[kSock], info[kFile]);
            if (dup2Result == 0)
                setvbuf(stdin, NULL, _IONBF, 0);
        }
    }

    void restore(int (&info)[2])
    {
        if (-1 != info[kSock] && -1 != info[kFile])
            _dup2(info[kFile], info[kSock]);
    }

} // namespace cc::console::platform
