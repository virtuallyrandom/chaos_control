#pragma once

#include <common/chrono.h>
#include <common/compiler.h>
#include <common/socket.h>
#include <common/stdarg.h>
#include <common/types.h>
#include <utility/callback_registrar.h>

struct Source
{
    enum Value : uint8_t
    {
        kApp,
        kCore,
        kUtility,
        kDatabase,

        kCount
    };
};

struct Level
{
    enum Value : uint8_t
    {
        kError,
        kStdErr,
        kWarning,
        kStdOut,
        kStatus,
        kInfo,
        kDebug,
        kTrace,

        kCount
    };
};

// On Windows, any time a debugger is attached, log messages <= kStatus are
// automatically sent to the output window of the debugger.
namespace cc
{
    class socket_watch;

    class console
    {
    public:
        using Callback = void (*)(void*, cc::system_clock::time_point, Source::Value, Level::Value, size_t, char const*);

        console(socket_watch&);
        ~console();

        void register_callback(Source::Value, Level::Value, Callback, void*);

        template <typename TypePtr>
        void register_callback(Source::Value v, Level::Value l, void(*cb)(TypePtr, cc::system_clock::time_point, Source::Value, Level::Value, size_t, char const*), TypePtr param)
        {
            Callback wcb;
            void* wprm;
            memcpy(&wcb, &cb, sizeof(wcb));
            memcpy(&wprm, &param, sizeof(wprm));
            register_callback(v, l, wcb, wprm);
        }

        void unregister_callback(Source::Value, Level::Value, Callback, void*);

        template <typename TypePtr>
        void unregister_callback(Source::Value v, Level::Value l, void(*cb)(TypePtr, cc::system_clock::time_point, Source::Value, Level::Value, size_t, char const*), TypePtr param)
        {
            Callback wcb;
            void* wprm;
            memcpy(&wcb, &cb, sizeof(wcb));
            memcpy(&wprm, &param, sizeof(wprm));
            unregister_callback(v, l, wcb, wprm);
        }

        PRINTF_FN(2, 3) void logf(Source::Value const src, Level::Value const lvl, PRINTF_ARG const char* const fmt, ...);
        PRINTF_FN(2, 3) void vlogf(Source::Value const src, Level::Value const lvl, PRINTF_ARG const char* const fmt, va_list args);

    private:
        static constexpr size_t kMaxLineSize = 1024;

        static void onStdOut(socket_type, console*);
        static void onStdErr(socket_type, console*);

        void distribute(const cc::system_clock::time_point, Source::Value, Level::Value, char const*, size_t);
        void onFile(socket_type, Source::Value, Level::Value, char(&)[kMaxLineSize], size_t&);

        socket_watch& m_sw;
        socket_type m_sockOut[2]{ kInvalidSocket, kInvalidSocket };
        int m_bindOut[2]{ -1, -1 };
        socket_type m_sockErr[2]{ kInvalidSocket, kInvalidSocket };
        int m_bindErr[2]{ -1, -1 };
        char m_stdoutBufferLine[kMaxLineSize]{};
        size_t m_stdoutBufferLineUsed{};
        char m_stderrBufferLine[kMaxLineSize]{};
        size_t m_stderrBufferLineUsed{};
        callback_registrar<Callback, void*> m_callback[Source::kCount + 1][Level::kCount + 1];

        console() = delete;
        compiler_disable_copymove(console);
    };
} // namespace cc
