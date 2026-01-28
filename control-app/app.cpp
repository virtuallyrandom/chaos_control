#include "app.h"

#include <common/debug.h>
#include <common/format.h>
#include <common/platform/windows.h>
#include <common/string.h>

#pragma comment(lib, "common.lib")
#pragma comment(lib, "containers.lib")
#pragma comment(lib, "json.lib")
#pragma comment(lib, "utility.lib")

namespace
{
    static constexpr char const* const kDefaultCrashAppDumpFile = "./crash_app.dmp";
} // namespace [anonymous]

app::app(int const argc, char const* const* const argv)
    : crash_handler("./crash_app.dmp", crash_handler_event)
    , args(argc, argv)
    , settings(cc::setting::read(args.get("settings", "app_settings.json")))
    , scheduler()
    , socket_watch(scheduler)
    , log_file(args.get("log", "app.log"), cc::file_mode::kWrite, cc::file_type::kText)
    , console(socket_watch)
    , database(console)
{
}

void app::crash_handler_event(void*)
{
    MessageBoxA(NULL, "crashed?\n\nApp crashed. Uh oh.", "Oshz...", MB_OK);
    exit(-1);
}

void app::on_log(app* const me, cc::system_clock::time_point tp, Source::Value const src, Level::Value const lvl, size_t const msgSize, char const* const msg)
{
    size_t const fullSize = msgSize + 64;
    char* const fullMsg = reinterpret_cast<char*>(alloca(fullSize));

    char const* sourceStr{ "Unknown" };
    switch (src)
    {
        case Source::kApp:      sourceStr = "App";      break;
        case Source::kCore:     sourceStr = "Core";     break;
        case Source::kUtility:  sourceStr = "Utility";  break;
        case Source::kDatabase: sourceStr = "Database"; break;
        case Source::kCount:    sourceStr = "All";      break;
        default: assert(false);
    }
    constexpr int sourceMaxLen = 4;

    char const* levelStr{ "Unknown" };
    constexpr int kLevelMaxLen = 7;
    switch (lvl)
    {
#define HANDLE_CASE(n) case Level::k##n: static_assert(sizeof(#n) <= kLevelMaxLen + 1, "kLevelMaxLen too small"); levelStr = #n; break;
        HANDLE_CASE(Error);
        HANDLE_CASE(StdErr);
        HANDLE_CASE(Warning);
        HANDLE_CASE(StdOut);
        HANDLE_CASE(Status);
        HANDLE_CASE(Info);
        HANDLE_CASE(Debug);
        HANDLE_CASE(Trace);
        case Level::kCount: levelStr = "All"; break;
        default: assert(false && "unhandled");
#undef HANDLE_CASE
    }

    cc::string dt = cc::format("{}", cc::current_zone()->to_local(tp));

    ::_snprintf_s(fullMsg, fullSize, _TRUNCATE, "%s %- *s %- *s %s\n", dt.c_str(), sourceMaxLen, sourceStr, kLevelMaxLen, levelStr, msg);
    if (cc::is_debugging())
        OutputDebugStringA(fullMsg);

    me->log_file.write(fullMsg);
    me->log_file.flush();
}
