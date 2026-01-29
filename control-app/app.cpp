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
    console.register_callback(Source::kCount, Level::kCount, on_log, this);
}

void app::crash_handler_event(void*)
{
    MessageBoxA(NULL, "crashed?\n\nApp crashed. Uh oh.", "Oshz...", MB_OK);
    exit(-1);
}

void app::on_log(app* const me, cc::system_clock::time_point tp, Source::Value const src, Level::Value const lvl, size_t const msgSize, char const* const msg)
{
    size_t const full_size = msgSize + 64;
    char* const full_msg = reinterpret_cast<char*>(alloca(full_size));

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

    char const* level_str{ "Unknown" };
    constexpr int kLevelMaxLen = 7;
    switch (lvl)
    {
#define HANDLE_CASE(n) case Level::k##n: static_assert(sizeof(#n) <= kLevelMaxLen + 1, "kLevelMaxLen too small"); level_str = #n; break;
        HANDLE_CASE(Error);
        HANDLE_CASE(StdErr);
        HANDLE_CASE(Warning);
        HANDLE_CASE(StdOut);
        HANDLE_CASE(Status);
        HANDLE_CASE(Info);
        HANDLE_CASE(Debug);
        HANDLE_CASE(Trace);
        case Level::kCount: level_str = "All"; break;
        default: assert(false && "unhandled");
#undef HANDLE_CASE
    }

    cc::string dt = cc::format("{}", cc::current_zone()->to_local(tp));

    ::_snprintf_s(full_msg, full_size, _TRUNCATE, "%s %- *s %- *s %s\n", dt.c_str(), sourceMaxLen, sourceStr, kLevelMaxLen, level_str, msg);

    if (cc::is_debugging())
    {
        cc::unique_lock lock(me->log_lock);
        OutputDebugStringA(full_msg);
    }

//    printf("%s", full_msg);

    me->log_file.write(full_msg);
    me->log_file.flush();
}
