#include <fcntl.h>
#include <io.h>

#include <common/assert.h>
#include <utility/platform/console.h>

namespace cc::console_platform
{
    constexpr int kFile = 0;
    constexpr int kSock = 1;

    // possibly revisit: https://stackoverflow.com/questions/311955/redirecting-cout-to-a-console-in-windows
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

            // was not associated with an output stream
            if (-2 == info[kFile])
            {
                assert(false && "Console handle not associated with an output stream. Did you AllocConsole and not freopen stdout?");
                return;
            }

            int const edup = _dup2(info[kSock], info[kFile]);
            assert(0 == edup);
            if (0 == edup)
            {
                int const esvb = setvbuf(from, NULL, _IONBF, 0);
                assert(0 == esvb);
            }
        }
    }

    void restore(int (&info)[2])
    {
        if (-1 != info[kSock] && -1 != info[kFile])
            _dup2(info[kFile], info[kSock]);
    }

} // namespace cc::console::platform
