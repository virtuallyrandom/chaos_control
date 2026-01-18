#pragma once

#include <common/chrono.h>
#include <common/types.h>

using socket_type = uintptr_t;

constexpr socket_type kInvalidSocket = ~socket_type(0);

constexpr int SHUT_RD = 0;
constexpr int SHUT_RDWR = 2;

struct iovec
{
    void* iov_base;
    size_t iov_len;
};

struct sockaddr;

namespace cc::socket
{
    extern const uint32_t kFioNRead;
    extern const uint32_t kFioNBio;

    constexpr size_t kMaxAddressString = 16;
    struct network_interface
    {
        char interface_address[kMaxAddressString];
        char broadcast_address[kMaxAddressString];
        char netmask_address[kMaxAddressString];
        bool is_up : 1;
        bool is_point_to_point : 1;
        bool is_loopback : 1;
        bool can_broadcast : 1;
        bool can_multicast : 1;
    };

    enum Domain
    {
        kInetV4,
        kInetV6,
    };

    enum Type
    {
        kStream,
        kDgram,
    };

    enum Protocol
    {
        kTcp,
        kUdp,
    };

    enum Direction
    {
        kSend = 1 << 0,
        kRecv = 1 << 1,
        kBoth = kSend | kRecv,
    };

    enum Opt
    {
        kPeek, // MSG_PEEK
    };

    bool initialize();
    void shutdown();

    uint32_t get_error();

    size_t get_interfaces(network_interface* const, size_t count);

    template<size_t Count>
    size_t get_interfaces(network_interface(&iface)[Count])
    {
        return get_interfaces(&iface[0], Count);
    }

    // helper functions
    socket_type TCPListen(const uint16_t port, const network_interface* optional_interface = nullptr);
    socket_type TCPConnect(const char* const address, const uint16_t port);

    using sockaddr = struct ::sockaddr;

    socket_type socket(Domain, Type, Protocol);

    size_t select(socket_type* read, size_t read_count, socket_type* write, size_t write_count, socket_type* error, size_t error_count, microseconds);

    size_t select(socket_type read = kInvalidSocket, socket_type write = kInvalidSocket, socket_type error = kInvalidSocket, microseconds const timeout = microseconds{ 0 });

    template<size_t ReadCount, size_t WriteCount, size_t ErrorCount>
    size_t select(socket_type read[ReadCount], socket_type write[WriteCount], socket_type error[ErrorCount], microseconds const timeout = microseconds{ 0 })
    {
        return select(read, ReadCount, write, WriteCount, error, ErrorCount, timeout);
    }

    socket_type accept(socket_type sck, sockaddr* addr, int* addr_len);

    int ioctl(socket_type, uint32_t cmd, uint32_t* argp);
    void shutdown(socket_type, Direction);
    int close(socket_type);
    int writev(socket_type, const struct iovec* const vec, const int count);

    // create a pair of sockets that can be used to communicate with each other.
    // windows requires AF_INET, SOCK_STREAM, IPPROTO_TCP
    // linux requires AF_UNIX/AF_LOCAL/AF_TIPC, ?, ?
    // (note that this is not posix compliant)
    int socketpair(Domain, Type, Protocol, socket_type sv[2]);

    // these are different from the standard in that they'll continue to
    // send or recv when it would otherwise bail due to internal buffers
    // being full.
    int recv_all(socket_type, void* const, const size_t size, const int flags);
    int send_all(socket_type, const void* const, const size_t size, const int flags);

    int recv(socket_type, void* const, const size_t size, const int flags);
    int send(socket_type, const void* const, const size_t size, const int flags);

    // return true if 'addr' is a member of 'base', false otherwise.
    // base can contain these mask variations:
    //   * - matches all addresses
    //   192.*.0.1 - exact match except * which is any value
    bool is_member(const char* base, const char* addr);
} // namespace cc::socket
