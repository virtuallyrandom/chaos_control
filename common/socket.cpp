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
        int referenceCount{};

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
        if (0 == s_ctx.referenceCount++)
        {
#if defined( _WIN32 )
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
                return false;
#endif // defined( _WIN32 )
        }

        return true;
    }

    void shutdown()
    {
        int const oldCount = s_ctx.referenceCount--;

        // just went to zero, close out the global context
        if (oldCount == 1)
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

    size_t get_interfaces(network_interface* const result, size_t const resultCount)
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

        size_t const count = cc::min(size / sizeof(ifaces[0]), resultCount);
        for (size_t i = 0; i < count; i++)
        {
            INTERFACE_INFO& iface = ifaces[i];

            network_interface& ri = result[i];

            sockaddr_in* const ifaceAddr = (sockaddr_in*)&iface.iiAddress;
            inet_ntop(ifaceAddr->sin_family, &ifaceAddr->sin_addr, ri.interface_address, sizeof(ri.interface_address));

            sockaddr_in* const bcastAddr = (sockaddr_in*)&iface.iiBroadcastAddress;
            inet_ntop(bcastAddr->sin_family, &bcastAddr->sin_addr, ri.broadcast_address, sizeof(ri.broadcast_address));

            sockaddr_in* const netmaskAddr = (sockaddr_in*)&iface.iiNetmask;
            inet_ntop(netmaskAddr->sin_family, &netmaskAddr->sin_addr, ri.netmask_address, sizeof(ri.netmask_address));

            ri.is_up = (iface.iiFlags & IFF_UP) == IFF_UP;
            ri.is_point_to_point = (iface.iiFlags & IFF_POINTTOPOINT) == IFF_POINTTOPOINT;
            ri.is_loopback = (iface.iiFlags & IFF_LOOPBACK) == IFF_LOOPBACK;
            ri.can_broadcast = (iface.iiFlags & IFF_BROADCAST) == IFF_BROADCAST;
            ri.can_multicast = (iface.iiFlags & IFF_MULTICAST) == IFF_MULTICAST;
        }

        close(sck);

        return count;
    }

    socket_type TCPListen(uint16_t const port, cc::socket::network_interface const* const netIface)
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
        char portName[6];
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        _snprintf_s(portName, sizeof(portName), "%hu", port);

        rv = ::getaddrinfo(NULL, portName, &hints, &result);
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
            if (netIface != nullptr)
                inet_pton(AF_INET, netIface->interface_address, &addr.sin_addr.s_addr);
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

        char portStr[6];

        snprintf(portStr, sizeof(portStr), "%hu", port);
        portStr[sizeof(portStr) - 1] = 0;

        struct addrinfo* result;
        int rv = getaddrinfo(address, portStr, &info, &result);
        if (rv != 0)
            return INVALID_SOCKET;

        socket_type client = INVALID_SOCKET;

        for (struct addrinfo* resultPtr = result; resultPtr != nullptr; resultPtr = resultPtr->ai_next)
        {
            struct sockaddr_in addr;
            if (resultPtr->ai_addrlen == sizeof(addr))
            {
                client = ::socket(resultPtr->ai_family, resultPtr->ai_socktype, resultPtr->ai_protocol);
                if (client != INVALID_SOCKET)
                {
                    memset(&addr, 0, sizeof(addr));
                    memcpy(&addr.sin_addr, &((struct sockaddr_in*)resultPtr->ai_addr)->sin_addr, sizeof(addr.sin_addr));
                    addr.sin_port = htons(port);
                    addr.sin_family = (ADDRESS_FAMILY)resultPtr->ai_family;
                    rv = connect(client, (struct ::sockaddr*)&addr, (int)resultPtr->ai_addrlen);
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

    size_t select(socket_type* const read, size_t const readCount, socket_type* const write, size_t const writeCount, socket_type* const error, size_t const errorCount, microseconds timeout)
    {
        fd_set readSet;
        fd_set writeSet;
        fd_set errorSet;

        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);

        socket_type hi = 0;

        size_t readUsed{ 0 };
        for (size_t i = 0; i < readCount; i++)
        {
            if (read[i] == kInvalidSocket)
                continue;
            FD_SET(read[i], &readSet);
            hi = cc::max(hi, read[i]);
            readUsed++;
        }

        size_t writeUsed{ 0 };
        for (size_t i = 0; i < writeCount; i++)
        {
            if (write[i] == kInvalidSocket)
                continue;
            FD_SET(write[i], &writeSet);
            hi = cc::max(hi, write[i]);
            writeUsed++;
        }

        size_t errorUsed{ 0 };
        for (size_t i = 0; i < errorCount; i++)
        {
            if (error[i] == kInvalidSocket)
                continue;
            FD_SET(error[i], &errorSet);
            hi = cc::max(hi, error[i]);
            errorUsed++;
        }

        if (readUsed == 0 && writeUsed == 0 && errorUsed == 0)
            return 0;

        seconds const sec = duration_cast<seconds>(timeout);
        microseconds const ms = timeout - sec;

        timeval tv;
        tv.tv_sec = truncate_cast<long>(sec.count()) + 1;
        tv.tv_usec = truncate_cast<long>(ms.count());
        int const rv = ::select(truncate_cast<int>(hi + 1), readUsed ? &readSet : nullptr, writeUsed ? &writeSet : nullptr, errorUsed ? &errorSet : nullptr, &tv);

        if (rv < 0)
        {
            int const e = WSAGetLastError();
            (void)e;
            return kInvalidSocket;
        }
        return truncate_cast<socket_type>(rv);
    }

    socket_type accept(socket_type sck, sockaddr* addr, int* addrLen)
    {
        return ::accept(sck, addr, addrLen);
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

    int recvAll(socket_type sck, void* const data, size_t const dataSize, int const flags)
    {
        int const maxReadChunkSize = INT32_MAX;

        size_t remain = dataSize;
        char* ptr = reinterpret_cast<char*>(data);
        while (remain != 0)
        {
            int const count = remain > maxReadChunkSize ? maxReadChunkSize : (int)remain;
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
        return dataSize > INT32_MAX ? INT32_MAX : (int)dataSize;
    }

    int sendAll(socket_type sck, void const* const data, size_t const dataSize, int const flags)
    {
        int const maxSendChunkSize = INT32_MAX;
        size_t remain = dataSize;
        char const* ptr = (char const*)data;
        while (remain != 0)
        {
            int const count = remain > maxSendChunkSize ? maxSendChunkSize : (int)remain;
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
        return dataSize > INT32_MAX ? INT32_MAX : (int)dataSize;
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

        char const* basePtr = base;
        char const* addrPtr = addr;
        for (size_t index = 0; ; index++)
        {
            if (*basePtr != '*' && *addrPtr != '*')
            {
                // todo: detect hex and update radix

                size_t const a = strtoul(basePtr, nullptr, radix);
                size_t const b = strtoul(addrPtr, nullptr, radix);
                if ( a != b)
                    return false;
            }

            basePtr = strstr(basePtr, ".");
            addrPtr = strstr(addrPtr, ".");

            assert(!!basePtr == !!addrPtr && "Length mismatch");

            if (basePtr == nullptr && addrPtr == nullptr)
                return true;

            basePtr++;
            addrPtr++;
        }

        compiler_unreachable;
    }
} // namespace cc::socket
