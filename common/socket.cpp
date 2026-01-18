#include <common/platform/winsock.h>

#include <common/socket.h>

#include <common/buildmsg.h>
#include <common/compiler.h>
#include <common/math.h>
#include <common/string.h>
#include <common/types.h>

#include <malloc.h>

#pragma comment(lib,"ws2_32.lib")

namespace
{
    struct socket_context
    {
        int reference_count{};

        socket_context() = default;
        ~socket_context() = default;

        socket_context(socket_context const&) = delete;
        socket_context(socket_context&&) = delete;
        socket_context& operator=(socket_context const&) = delete;
        socket_context& operator=(socket_context&&) = delete;
    };

    static socket_context s_ctx;

    constexpr static int domain_of(cc::socket::Domain const d)
    {
        switch (d)
        {
            case cc::socket::kInetV4:
                return AF_INET;

            case cc::socket::kInetV6:
                return AF_INET6;

            default:
                assert(false && "Unhandled");
        }

        return 0; // AF_UNSPEC
    }

    constexpr static int type_of(cc::socket::Type const t)
    {
        switch (t)
        {
            case cc::socket::kStream:
                return SOCK_STREAM;
            case cc::socket::kDgram:
                return SOCK_DGRAM;
            default:
                assert(false && "Unhandled");
        }

        return 0;
    }

    constexpr static int protocol_of(cc::socket::Protocol const p)
    {
        switch (p)
        {
            case cc::socket::kTcp:
                return IPPROTO_TCP;

            case cc::socket::kUdp:
                return IPPROTO_UDP;

            default:
                assert(false && "Unhandled");
        }

        return 0;
    }
} // namespace [anonymous]

namespace cc::socket
{
    const uint32_t kFioNRead = FIONREAD;
    const uint32_t kFioNBio = FIONBIO;

    bool initialize()
    {
        if (0 == s_ctx.reference_count++)
        {
#if defined( _WIN32 )
            WSADATA wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
                return false;
#endif // defined( _WIN32 )
        }

        return true;
    }

    void shutdown()
    {
        int const old_count = s_ctx.reference_count--;

        // just went to zero, close out the global context
        if (old_count == 1)
        {
#if defined( _WIN32 )
            WSACleanup();
#endif // defined( _WIN32 )
        }
    }

    uint32_t get_error()
    {
#if defined( _WIN32 )
        return (uint32_t)WSAGetLastError();
#else // !defined( _WIN32 )
        return errno;
#endif // !defined( _WIN32 )
    }

    size_t get_interfaces(network_interface* const result, size_t const result_count)
    {
        int rv;

        socket_type sck = ::WSASocketW(AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
        if (sck == SOCKET_ERROR)
            return 0;

        constexpr size_t kMaxInterfaces = 16;
        INTERFACE_INFO ifaces[kMaxInterfaces];
        unsigned long size;
        rv = WSAIoctl(sck, SIO_GET_INTERFACE_LIST, 0, 0, &ifaces, sizeof(ifaces), &size, 0, 0);
        if (SOCKET_ERROR == rv)
        {
            close(sck);
            return 0;
        }

        size_t const count = cc::min(size / sizeof(ifaces[0]), result_count);
        for (size_t i = 0; i < count; i++)
        {
            INTERFACE_INFO& iface = ifaces[i];

            network_interface& ri = result[i];

            sockaddr_in* const iface_addr = (sockaddr_in*)&iface.iiAddress;
            inet_ntop(iface_addr->sin_family, &iface_addr->sin_addr, ri.interface_address, sizeof(ri.interface_address));

            sockaddr_in* const bcast_addr = (sockaddr_in*)&iface.iiBroadcastAddress;
            inet_ntop(bcast_addr->sin_family, &bcast_addr->sin_addr, ri.broadcast_address, sizeof(ri.broadcast_address));

            sockaddr_in* const netmask_addr = (sockaddr_in*)&iface.iiNetmask;
            inet_ntop(netmask_addr->sin_family, &netmask_addr->sin_addr, ri.netmask_address, sizeof(ri.netmask_address));

            ri.is_up = (iface.iiFlags & IFF_UP) == IFF_UP;
            ri.is_point_to_point = (iface.iiFlags & IFF_POINTTOPOINT) == IFF_POINTTOPOINT;
            ri.is_loopback = (iface.iiFlags & IFF_LOOPBACK) == IFF_LOOPBACK;
            ri.can_broadcast = (iface.iiFlags & IFF_BROADCAST) == IFF_BROADCAST;
            ri.can_multicast = (iface.iiFlags & IFF_MULTICAST) == IFF_MULTICAST;
        }

        close(sck);

        return count;
    }

    socket_type TCPListen(uint16_t const port, cc::socket::network_interface const* const net_iface)
    {
        struct addrinfo info;

        memset(&info, 0, sizeof(info));
        info.ai_family = AF_INET;
        info.ai_socktype = SOCK_STREAM;
        info.ai_protocol = IPPROTO_TCP;
        info.ai_flags = AI_PASSIVE;

        int rv = SOCKET_ERROR;
        socket_type listener = INVALID_SOCKET;

/*
        struct addrinfo* result {};
        struct addrinfo hints {};
        char port_name[6];
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        _snprintf_s(port_name, sizeof(port_name), "%hu", port);

        rv = ::getaddrinfo(NULL, port_name, &hints, &result);
        if (rv != 0)
        {
        }
        ::freeaddrinfo(result);
*/

        for (;; )
        {
            listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listener == INVALID_SOCKET)
                break;

            int yes = 1;
            (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

            struct sockaddr_in addr;

            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            if (net_iface != nullptr)
                inet_pton(AF_INET, net_iface->interface_address, &addr.sin_addr.s_addr);
            else
                addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port);

            rv = ::bind(listener, (struct ::sockaddr const*)&addr, sizeof(struct ::sockaddr));
            if (rv == SOCKET_ERROR)
                break;

            rv = listen(listener, 1);
            if (rv == SOCKET_ERROR)
                break;

            break;
        }

        if (rv == SOCKET_ERROR)
        {
            close(listener);
            listener = INVALID_SOCKET;
        }

        return listener;
    }

    socket_type TCPConnect(char const* const address, uint16_t const port)
    {
        struct addrinfo info;

        memset(&info, 0, sizeof(info));
        info.ai_family = AF_INET;
        info.ai_socktype = SOCK_STREAM;
        info.ai_protocol = IPPROTO_TCP;
        info.ai_flags = 0;

        char port_str[6];

        snprintf(port_str, sizeof(port_str), "%hu", port);
        port_str[sizeof(port_str) - 1] = 0;

        struct addrinfo* result;
        int rv = getaddrinfo(address, port_str, &info, &result);
        if (rv != 0)
            return INVALID_SOCKET;

        socket_type client = INVALID_SOCKET;

        for (struct addrinfo* result_ptr = result; result_ptr != nullptr; result_ptr = result_ptr->ai_next)
        {
            struct sockaddr_in addr;
            if (result_ptr->ai_addrlen == sizeof(addr))
            {
                client = ::socket(result_ptr->ai_family, result_ptr->ai_socktype, result_ptr->ai_protocol);
                if (client != INVALID_SOCKET)
                {
                    memset(&addr, 0, sizeof(addr));
                    memcpy(&addr.sin_addr, &((struct sockaddr_in*)result_ptr->ai_addr)->sin_addr, sizeof(addr.sin_addr));
                    addr.sin_port = htons(port);
                    addr.sin_family = (ADDRESS_FAMILY)result_ptr->ai_family;
                    rv = connect(client, (struct ::sockaddr*)&addr, (int)result_ptr->ai_addrlen);
                    if (rv == 0)
                        return client;
                    close(client);
                }
            }
        }

        return INVALID_SOCKET;
    }

    socket_type socket(Domain const edomain, Type const etype, Protocol const eprotocol)
    {
        int const domain{ domain_of(edomain) };
        int const type{ type_of(etype) };
        int const protocol{ protocol_of(eprotocol) };
        return ::socket(domain, type, protocol);
    }

    size_t select(socket_type read, socket_type write, socket_type error, microseconds const timeout)
    {
        return select(&read, 1, &write, 1, &error, 1, timeout);
    }

    size_t select(socket_type* const read, size_t const read_count, socket_type* const write, size_t const write_count, socket_type* const error, size_t const error_count, microseconds timeout)
    {
        fd_set read_set;
        fd_set write_set;
        fd_set error_set;

        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);

        socket_type hi = 0;

        size_t read_used{ 0 };
        for (size_t i = 0; i < read_count; i++)
        {
            if (read[i] == kInvalidSocket)
                continue;
            FD_SET(read[i], &read_set);
            hi = cc::max(hi, read[i]);
            read_used++;
        }

        size_t write_used{ 0 };
        for (size_t i = 0; i < write_count; i++)
        {
            if (write[i] == kInvalidSocket)
                continue;
            FD_SET(write[i], &write_set);
            hi = cc::max(hi, write[i]);
            write_used++;
        }

        size_t error_used{ 0 };
        for (size_t i = 0; i < error_count; i++)
        {
            if (error[i] == kInvalidSocket)
                continue;
            FD_SET(error[i], &error_set);
            hi = cc::max(hi, error[i]);
            error_used++;
        }

        if (read_used == 0 && write_used == 0 && error_used == 0)
            return 0;

        seconds const sec = duration_cast<seconds>(timeout);
        microseconds const ms = timeout - sec;

        timeval tv;
        tv.tv_sec = truncate_cast<long>(sec.count()) + 1;
        tv.tv_usec = truncate_cast<long>(ms.count());
        int const rv = ::select(truncate_cast<int>(hi + 1), read_used ? &read_set : nullptr, write_used ? &write_set : nullptr, error_used ? &error_set : nullptr, &tv);

        if (rv < 0)
        {
            int const e = ::WSAGetLastError();
            (void)e;
            return kInvalidSocket;
        }
        return truncate_cast<socket_type>(rv);
    }

    socket_type accept(socket_type sck, sockaddr* addr, int* addr_len)
    {
        return ::accept(sck, addr, addr_len);
    }

    int ioctl(socket_type const s, uint32_t const cmd, uint32_t* const argp)
    {
        return ::ioctlsocket(s, static_cast<long>(cmd), reinterpret_cast<unsigned long*>(argp));
    }

    int close(socket_type s)
    {
        return ::closesocket(s);
    }

    void shutdown(socket_type s, Direction dir)
    {
        if (kBoth == dir)
            ::shutdown(s, SD_BOTH);
        else if ((kRecv & dir) == kRecv)
            ::shutdown(s, SD_RECEIVE);
        else if ((kSend & dir) == kSend)
            ::shutdown(s, SD_SEND);
    }

    int writev(socket_type const sck, struct iovec const* const vec, int const count)
    {
        if (count <= 0)
        {
            ::WSASetLastError(EINVAL);
            return -1;
        }

#pragma warning( push )
#pragma warning( disable : 6255 )
        WSABUF* const buf = (WSABUF*)::alloca(sizeof(WSABUF) * count);
#pragma warning( pop )

        BUILDMSG_FIXME("this doesn't handle uint64_t sizes correctly; it just truncates")
            for (int i = 0; i < count; i++)
            {
                buf[i].buf = (char*)vec[i].iov_base;
                //assert( ( ULONG)vec[ i ].iov_len == vec[ i ].iov_len );
                buf[i].len = (ULONG)vec[i].iov_len;
            }
        DWORD sent = 0;
        int err = WSASend(sck, buf, (DWORD)count, &sent, 0, nullptr, nullptr);
        if (err != 0)
            return -1;
        return cc::clamp(truncate_cast<int>(sent), 0, INT32_MAX);
    }

    // https://man7.org/linux/man-pages/man2/socketpair.2.html
    // for windows, https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/
    // type must be AF_RAW
    int socketpair(Domain edomain, Type etype, Protocol eprotocol, socket_type sv[2])
    {
        int const domain = domain_of(edomain);
        int const type = type_of(etype);
        int const protocol = protocol_of(eprotocol);

        sv[0] = INVALID_SOCKET;
        sv[1] = INVALID_SOCKET;

        int rv = SOCKET_ERROR;
        socket_type listener = INVALID_SOCKET;

        for (;; )
        {
            listener = ::WSASocketW(domain, type, protocol, nullptr, 0, 0);// ::socket(domain, type, protocol);
            if (listener == INVALID_SOCKET)
                break;

            int yes = 1;
            (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

            struct sockaddr_in addr;

            memset(&addr, 0, sizeof(addr));
            addr.sin_family = (ADDRESS_FAMILY)domain;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            rv = bind(listener, (struct ::sockaddr const*)&addr, sizeof(struct ::sockaddr));
            if (rv == SOCKET_ERROR)
                break;

            sockaddr_in sin;
            socklen_t len = sizeof(sin);
            if (getsockname(listener, (::sockaddr*)&sin, &len) == SOCKET_ERROR)
                break;

            rv = listen(listener, 1);
            if (rv == SOCKET_ERROR)
                break;

            socket_type sa = ::socket(domain, type, protocol);
            if (sa == INVALID_SOCKET)
                break;

            rv = connect(sa, (::sockaddr*)&sin, len);
            if (rv == SOCKET_ERROR)
            {
                close(sa);
                break;
            }

            socket_type sb = accept(listener, nullptr, nullptr);
            if (sb == INVALID_SOCKET)
            {
                close(sa);
                break;
            }

            sv[0] = sa;
            sv[1] = sb;
            break;
        }

        if (listener != INVALID_SOCKET)
            close(listener);

        return rv;
    }

    int recv_all(socket_type sck, void* const data, size_t const data_size, int const flags)
    {
        int const max_read_chunk_size = INT32_MAX;

        size_t remain = data_size;
        char* ptr = reinterpret_cast<char*>(data);
        while (remain != 0)
        {
            int const count = remain > max_read_chunk_size ? max_read_chunk_size : (int)remain;
            int const rv = ::recv(sck, ptr, count, flags);
            if (rv <= 0)
            {
                uint32_t const err = get_error();
                if (err == EAGAIN || err == EWOULDBLOCK)
                    continue;
                return rv;
            }
            ptr += rv;
            remain -= rv;
        }
        return data_size > INT32_MAX ? INT32_MAX : (int)data_size;
    }

    int send_all(socket_type sck, void const* const data, size_t const data_size, int const flags)
    {
        int const max_send_chunk_size = INT32_MAX;
        size_t remain = data_size;
        char const* ptr = (char const*)data;
        while (remain != 0)
        {
            int const count = remain > max_send_chunk_size ? max_send_chunk_size : (int)remain;
            int const rv = ::send(sck, ptr, count, flags);
            if (rv <= 0)
            {
                uint32_t const err = get_error();
                if (err == EAGAIN || err == EWOULDBLOCK)
                    continue;
                return rv;
            }
            ptr += rv;
            remain -= rv;
        }
        return data_size > INT32_MAX ? INT32_MAX : (int)data_size;
    }

    int recv(socket_type sck, void* const buf, size_t const size, int const flags)
    {
        return ::recv(sck, reinterpret_cast<char*>(buf), truncate_cast<int>(size), flags);
    }

    int send(socket_type sck, void const* const buf, size_t const size, int const flags)
    {
        return ::send(sck, reinterpret_cast<char const*>(buf), truncate_cast<int>(size), flags);
    }

    bool is_member(char const* base, char const* addr)
    {
        if (base == nullptr || addr == nullptr)
            return false;

        if (strcmp(base, "*") == 0 || strcmp(addr, "*") == 0)
            return true;

        // handle both ip4 and ip6
        int radix{ 10 };

        char const* base_ptr = base;
        char const* addr_ptr = addr;
        for (size_t index = 0; ; index++)
        {
            if (*base_ptr != '*' && *addr_ptr != '*')
            {
                // todo: detect hex and update radix

                size_t const a = strtoul(base_ptr, nullptr, radix);
                size_t const b = strtoul(addr_ptr, nullptr, radix);
                if ( a != b)
                    return false;
            }

            base_ptr = strstr(base_ptr, ".");
            addr_ptr = strstr(addr_ptr, ".");

            assert(!!base_ptr == !!addr_ptr && "Length mismatch");

            if (base_ptr == nullptr && addr_ptr == nullptr)
                return true;

            base_ptr++;
            addr_ptr++;
        }

        compiler_unreachable;
    }
} // namespace cc::socket
