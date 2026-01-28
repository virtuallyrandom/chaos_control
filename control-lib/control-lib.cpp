#include <common/atomic.h>
#include <common/algorithm.h>
#include <common/compiler.h>
#include <common/debug.h>
#include <common/file.h>
#include <common/format.h>
#include <common/hash.h>
#include <common/memory.h>
#include <common/socket.h>
#include <common/time.h>
#include <common/utility.h>
#include <common/platform/windows.h>
#include <containers/set.h>
#include <containers/static_freelist.h>
#include <containers/static_queue.h>
#include <containers/string.h>
#include <containers/unordered_map.h>
#include <script/lexer.h>
#include <utility/args.h>
#include <utility/console.h>
#include <utility/crash_handler.h>
#include <utility/database.h>
#include <utility/processor_info.h>
#include <utility/scheduler.h>
#include <utility/service.h>
#include <utility/setting.h>
#include <utility/socket_watch.h>

#include <utility/callback_registrar.h>

#include "control-lib.h"

#pragma warning(push,1)
#include <imgui.h>
#include <json.h>
#pragma warning(pop)

#include <cstdarg>

#pragma comment(lib, "common.lib")
#pragma comment(lib, "containers.lib")
#pragma comment(lib, "json.lib")
#pragma comment(lib, "script.lib")
#pragma comment(lib, "utility.lib")

constexpr uint16_t kClientPort = 48094;
constexpr uint16_t kLocalPort = 48095;

struct connection_id { int64_t value{ -1 }; };

namespace cc
{
    extern bool gContainerTestsPass;
} // namespace cc

static void on_crash(void* const)
{
    ::MessageBoxA(NULL, "crashed?\n\nWell, that sucks!", "Oshz...", MB_OK);
    ::exit(-1);
}

static void collect_interfaces(cc::socket::network_interface* ifaces, size_t const iface_count, const cc::setting& interface_settings, cc::set<cc::socket::network_interface*>& result)
{
    for (size_t i = 0; i < iface_count; i++)
    {
        for (const cc::pair<const cc::string, cc::setting> pr : interface_settings)
        {
            const cc::setting& setting = pr.second;

            if (setting.contains("interfaceAddress"))
            {
                const cc::string& iface_addr = setting["interfaceAddress"];
                if (!cc::socket::is_member(iface_addr.c_str(), ifaces[i].interface_address))
                    continue;
            }

            const cc::string& iface_addr = setting["interfaceAddress"];
            if (!cc::socket::is_member(iface_addr.c_str(), ifaces[i].interface_address))
                continue;

            result.emplace(&ifaces[i]);
        }
    }
}

static void start_listeners(cc::set<cc::socket::network_interface*>& use_interfaces,
                            uint16_t const port,
                            cc::vector<socket_type>& listener_sockets,
                            cc::socket_watch& socket_watch,
                            cc::on_wake_callback const on_listener,
                            void* const param)
{
    for (const cc::socket::network_interface* const iface : use_interfaces)
    {
        socket_type const listener = cc::socket::TCPListen(port, iface);
        if (kInvalidSocket != listener)
        {
            listener_sockets.push_back(listener);
            socket_watch.add(listener, on_listener, param);
        }
    }
}

struct control_lib
{
    cc::utility::crash_handler crash_handler;
    cc::scheduler scheduler;
    uint8_t pad0[8]{};
    cc::socket_watch socket_watch;
    uint8_t pad1[8]{};
    cc::console console;
    cc::database database;
    cc::vector<socket_type> listener_sockets;
    cc::file log_file;
    uint8_t pad2[48]{};

    control_lib()
        : crash_handler("./crash.dmp", on_crash)
        , scheduler()
        , socket_watch(scheduler)
        , console(socket_watch)
        , database(console)
    {
        assert(cc::gContainerTestsPass);

        log_file.open("control.log", cc::file_mode::kWrite, cc::file_type::kText);
        verify(true, log_file);

        console.register_callback(Source::kCount, Level::kCount, on_log, this);
    }

private:
    static void on_log(control_lib* const me, cc::system_clock::time_point tp, Source::Value const src, Level::Value const lvl, size_t const msgSize, char const* const msg)
    {
        size_t const full_size = msgSize + 64;
        char* const full_msg = reinterpret_cast<char*>(alloca(full_size));

        char const* source_str{ "Unknown" };
        switch (src)
        {
            case Source::kApp:      source_str = "App";      break;
            case Source::kCore:     source_str = "Core";     break;
            case Source::kUtility:  source_str = "Utility";  break;
            case Source::kDatabase: source_str = "Database"; break;
            case Source::kCount:    source_str = "All";      break;
            default: assert(false);
        }
        constexpr int source_max_len = 4;

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

        ::_snprintf_s(full_msg, full_size, _TRUNCATE, "%s %- *s %- *s %s\n", dt.c_str(), source_max_len, source_str, kLevelMaxLen, level_str, msg);
        if (cc::is_debugging())
            ::OutputDebugStringA(full_msg);

        me->log_file.write(full_msg);
        me->log_file.flush();
    }
};

static void on_client_socket(socket_type const sck, control_lib* const me)
{
    char msg[512];

    int rv = cc::socket::recv(sck, msg, sizeof(msg) - 1, 0);
    if (rv <= 0)
    {
        cc::socket::close(sck);
        me->socket_watch.remove(sck);
        return;
    }

    // todo:
    //   - capture message
    //   - punt to registered system(s)

    msg[rv] = 0;
    me->console.logf(Source::kApp, Level::kTrace, "sck[%d] recv: %s", sck, msg);
}

static void on_local_socket(socket_type const sck, control_lib* const me)
{
    char msg[512];

    int rv = cc::socket::recv(sck, msg, sizeof(msg) - 1, 0);
    if (rv <= 0)
    {
        cc::socket::close(sck);
        me->socket_watch.remove(sck);
        return;
    }

    // todo:
    //   - capture message
    //   - punt to registered system(s)

    msg[rv] = 0;
    me->console.logf(Source::kApp, Level::kTrace, "sck[%d] recv: %s", sck, msg);
}

static void on_client_listener(socket_type const sck, control_lib* const me)
{
    cc::socket::sockaddr addr;
    int addrlen = sizeof(addr);

    socket_type client = cc::socket::accept(sck, &addr, &addrlen);
    if (client == kInvalidSocket)
        return;

    char addrStr[16];
    getnameinfo(&addr, addrlen, addrStr, sizeof(addrStr), nullptr, 0, NI_NUMERICHOST);

    sockaddr_in listenerAddr;
    socklen_t listenerAddrLen = sizeof(listenerAddr);

    if (-1 != getpeername(sck, (sockaddr*)&listenerAddr, &listenerAddrLen))
        me->console.logf(Source::kApp, Level::kTrace, "Accepting connection from %s on port %hu", addrStr, ntohs(listenerAddr.sin_port));

    // set to nonblocking as we just want to push waking events into it
    uint32_t blocking = 0;
    (void)cc::socket::ioctl(sck, cc::socket::kFioNBio, &blocking);

    connection_id conid;
    cc::database::script script = me->database.add("");
    if (false == me->database.exec("INSERT INTO connection (address, port) VALUES (?, ?);", addrStr, kClientPort))
        conid.value = me->database.last_insert_rowid();

    me->socket_watch.add(client, on_client_socket, me);
}

static void on_local_listener(socket_type const sck, control_lib* const me)
{
    cc::socket::sockaddr addr;
    int addrlen = sizeof(addr);

    socket_type client = cc::socket::accept(sck, &addr, &addrlen);
    if (client == kInvalidSocket)
        return;

    char addrStr[16];
    getnameinfo(&addr, addrlen, addrStr, sizeof(addrStr), nullptr, 0, NI_NUMERICHOST);

    sockaddr_in listenerAddr;
    socklen_t listenerAddrLen = sizeof(listenerAddr);

    if (getpeername(sck, (sockaddr*)&listenerAddr, &listenerAddrLen) != -1)
        me->console.logf(Source::kApp, Level::kTrace, "Accepting connection from %s on port %hu", addrStr, ntohs(listenerAddr.sin_port));

    // set to nonblocking as we just want to push waking events into it
    uint32_t blocking = 0;
    (void)cc::socket::ioctl(sck, cc::socket::kFioNBio, &blocking);

    connection_id conid;
    if (SQLITE_OK == me->database.exec("INSERT INTO connection (address, port) VALUES (?, ?);", addrStr, kLocalPort))
        conid.value = me->database.last_insert_rowid();

    me->socket_watch.add(client, on_local_socket, me);
}

control_lib* control_create(size_t, char const* const*)
{
    cc::socket::initialize();

    control_lib* const lib = new control_lib;

    lib->console.logf(Source::kApp, Level::kStatus, "Control initialized");

    return lib;
}

bool control_destroy(control_lib* const lib)
{
    if (nullptr == lib)
        return false;

    lib->console.logf(Source::kApp, Level::kStatus, "Control destroying");

    delete lib;

    cc::socket::shutdown();

    return true;
}
bool control_start(control_lib* const lib)
{
    if (nullptr == lib)
        return false;

    lib->console.logf(Source::kApp, Level::kStatus, "Control starting");

    const cc::setting settings = cc::setting::read("settings.json");

    // start the database
    const cc::string& dbPath = settings["/Database/path"];

    if (!lib->database.open(dbPath.c_str()))
    {
        if (!lib->database.create(dbPath.c_str()))
        {
            lib->console.logf(Source::kApp, Level::kError, "Unable to create database '%s'", dbPath.c_str());
            return false;
        }

        const cc::string& dbCreateScript = settings["/Database/db_initialize"];
        cc::unique_ptr<char[]> file = cc::file::load(dbCreateScript.c_str(), cc::file_type::kText);

        if (!lib->database.exec(file.get()))
        {
            lib->console.logf(Source::kApp, Level::kError, "Failed to run databaes creation script");
            return false;
        }
    }

#if 0
    char const* const insertSQL = "INSERT INTO packet (connectionId, data) VALUES (?, ?);";
    char const* const selectSQL = "SELECT * from packet;";
    char const* const selectAllSQL = "SELECT data FROM packet WHERE connectionID = ?";

    cc::database::script insertScript = db.add(insertSQL);

    struct local
    {
        static bool onEntryWithParam(char const* const param, size_t num, char const* const* names, cc::database::variant_type const* values)
        {
            (void)num;
            (void)param;
            (void)names;
            (void)values;
            return true;
        }
    };

    db.exec(selectScript, local::onEntryWithParam, selectSQL);

    db.remove(selectScript);
#endif // 0

    cc::socket::network_interface ifaces[8];
    const size_t ifaceCount = cc::socket::get_interfaces(ifaces);

    // for each of the network interfaces, start a client listener. it accepts
    // incoming client requests and adds them to the socket watch. when the client
    // sends data, the client socket reads the data and forwards it to registrants.

    cc::set<cc::socket::network_interface*> use_interfaces;

    // start client listeners (devices, status updates, etc)
    collect_interfaces(ifaces,
                       ifaceCount,
                       settings["/Network/Client/Interface"],
                       use_interfaces);
    start_listeners(use_interfaces,
                    kClientPort,
                    lib->listener_sockets,
                    lib->socket_watch,
                    (cc::on_wake_callback)on_client_listener,
                    lib);

    use_interfaces.clear();

    // set up a local listeners (control UI, etc)
    collect_interfaces(ifaces,
                       ifaceCount,
                       settings["/Network/Local/Interface"],
                       use_interfaces);
    start_listeners(use_interfaces,
                    kLocalPort,
                    lib->listener_sockets,
                    lib->socket_watch,
                    (cc::on_wake_callback)on_local_listener,
                    lib);

    lib->console.logf(Source::kApp, Level::kStatus, "Control started");

    return true;
}

bool control_stop(control_lib* const lib)
{
    if (nullptr == lib)
        return false;

    lib->console.logf(Source::kApp, Level::kStatus, "Control stopping");

    for (socket_type sck : lib->listener_sockets)
    {
        lib->socket_watch.remove(sck);
        cc::socket::close(sck);
    }

    lib->listener_sockets.clear();
    lib->database.close();

    lib->console.logf(Source::kApp, Level::kStatus, "Control stopped");

    return true;
}

bool control_update(control_lib* const lib)
{
    if (nullptr == lib)
        return false;

    return true;
}

extern "C" __declspec(dllexport) void get_control_api(control_api* const api)
{
    api->create = control_create;
    api->destroy = control_destroy;
    api->start = control_start;
    api->stop = control_stop;
    api->update = control_update;
}
