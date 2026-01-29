#pragma once

#include <common/file.h>
#include <common/mutex.h>
#include <utility/args.h>
#include <utility/console.h>
#include <utility/crash_handler.h>
#include <utility/database.h>
#include <utility/scheduler.h>
#include <utility/setting.h>
#include <utility/socket_watch.h>

struct app
{
    cc::utility::crash_handler crash_handler;
    cc::args args;
    cc::setting settings;
    cc::scheduler scheduler;
    cc::socket_watch socket_watch;
    cc::file log_file;
    cc::shared_timed_mutex log_lock;
    cc::console console;
    cc::database database;

    app(int const argc, char const* const* const argv);

private:
    static void crash_handler_event(void*);
    static void on_log(app*, cc::system_clock::time_point, Source::Value, Level::Value, size_t, char const*);
};
