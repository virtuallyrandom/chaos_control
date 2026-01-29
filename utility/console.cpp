#include <utility/console.h>

#include <common/chrono.h>
#include <common/debug.h>
#include <common/file.h>
#include <common/platform/windows.h>
#include <common/stdio.h>
#include <utility/platform/console.h>
#include <utility/socket_watch.h>

namespace cc
{
    console::console(socket_watch& sw)
        : m_sw(sw)
    {
        socket::initialize();

        socket::socketpair(socket::kInetV4, socket::kStream, socket::kTcp, m_sockOut);
        socket::socketpair(socket::kInetV4, socket::kStream, socket::kTcp, m_sockErr);

        sw.add(m_sockOut[0], onStdOut, this);
        sw.add(m_sockErr[0], onStdErr, this);

        console_platform::redirect(stdout, m_sockOut[1], m_bindOut);
        console_platform::redirect(stderr, m_sockErr[1], m_bindErr);
    }

    console::~console()
    {
        console_platform::restore(m_bindOut);
        console_platform::restore(m_bindErr);

        socket::close(m_sockOut[0]);
        socket::close(m_sockOut[1]);

        socket::close(m_sockErr[0]);
        socket::close(m_sockErr[1]);

        socket::shutdown();
    }

    void console::register_callback(Source::Value const src, Level::Value const lvl, Callback const cb, void* const param)
    {
        m_callback[src][lvl].add(cb, param);
    }

    void console::unregister_callback(Source::Value const src, Level::Value const lvl, Callback const cb, void* const param)
    {
        m_callback[src][lvl].remove(cb, param);
    }

    PRINTF_FN(2, 3) void console::logf(Source::Value const src, Level::Value const lvl, PRINTF_ARG const char* const fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlogf(src, lvl, fmt, args);
        va_end(args);
    }

    PRINTF_FN(2, 3) void console::vlogf(Source::Value const src, Level::Value const lvl, PRINTF_ARG const char* const fmt, va_list args)
    {
        char tmp[256];
        char* msg{ tmp };
        size_t msgSize{ sizeof(tmp) };

        int const rv = ::vsnprintf_s(msg, msgSize, _TRUNCATE, fmt, args);

        if (rv < 0)
        {
            int const newSize = ::_vscprintf(fmt, args) + 1;
            if (newSize < 0)
                return;

            msgSize = truncate_cast<size_t>(newSize);
            msg = reinterpret_cast<char*>(alloca(msgSize + 1));
            ::vsnprintf_s(msg, msgSize, _TRUNCATE, fmt, args);
        }

        distribute(cc::system_clock::now(), src, lvl, msg, msgSize);
    }

    void console::onStdOut(socket_type const sck, console* const me)
    {
        me->onFile(sck, Source::kApp, Level::kStdOut, me->m_stdoutBufferLine, me->m_stdoutBufferLineUsed);
    }

    void console::onStdErr(socket_type const sck, console* const me)
    {
        me->onFile(sck, Source::kApp, Level::kStdErr, me->m_stderrBufferLine, me->m_stderrBufferLineUsed);
    }

    void console::distribute(const cc::system_clock::time_point tp, Source::Value const src, Level::Value const lvl, char const* const msg, size_t const msgSize)
    {
        m_callback[src][lvl].invoke(tp, src, lvl, msgSize, msg);
        m_callback[Source::kCount][lvl].invoke(tp, src, lvl, msgSize, msg);
        m_callback[Source::kCount][Level::kCount].invoke(tp, src, lvl, msgSize, msg);
    }

    void console::onFile(socket_type const sck, Source::Value const src, Level::Value const lvl, char(&lineBuffer)[kMaxLineSize], size_t& lineBufferUsed)
    {
        const cc::system_clock::time_point tp{ cc::system_clock::now() };

        for (;;)
        {
            uint32_t count{};
            int rv = cc::socket::ioctl(sck, cc::socket::kFioNRead, &count);
            if (0 != rv)
                break;

            if (0 == count)
                break;

            assert(lineBufferUsed + count < kMaxLineSize && "Out of buffer size");

            count = cc::min(count, kMaxLineSize - lineBufferUsed);

            int const len = cc::socket::recv(sck, lineBuffer + lineBufferUsed, count, 0);
            if (len < 0)
                break;

            if (len == 0)
                break;

            lineBufferUsed += len;
            lineBuffer[lineBufferUsed] = 0;

            // no eol encountered yet so keep buffering
            if (lineBuffer[lineBufferUsed - 1] != '\n')
                break;

            char* next{};
            char* str = strtok_s(lineBuffer, "\n", &next);
            while (str)
            {
                ptrdiff_t used = next - str - 1;

                if (used >= 2 && next[-2] == '\r')
                {
                    next[-2] = 0;
                    used--;
                }
                distribute(tp, src, lvl, str, truncate_cast<size_t>(used));
                str = strtok_s(nullptr, "\n", &next);
            }

            lineBufferUsed = 0;
        }
    }
} // namespace cc::console
