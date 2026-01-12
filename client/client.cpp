#include <common/compiler.h>
#include <common/socket.h>
#include <common/platform/windows.h>

#include <utility/crash_handler.h>

#include <cstdio>

static void on_crash(void* const)
{
    MessageBoxA(NULL, "crashed?\n\nWell, that sucks!", "Oshz...", MB_OK);
}

#if defined( _WINDOWS )
int CALLBACK WinMain(_In_ HINSTANCE inst, _In_opt_ HINSTANCE prev, _In_ LPSTR cmd, _In_ int show)
{
#else /* unknown */
int main(int const, char const**)
{
#endif /* unknown */

    cc::utility::crash_handler crash_handler("./client.dmp", on_crash);

    cc::socket::initialize();

    socket_type con = kInvalidSocket;

    for (;;)
    {
//            con = cc::socket::TCPConnect("127.0.0.1", 48094);
        con = cc::socket::TCPConnect("192.168.1.140", 48094);
        printf(con == kInvalidSocket ? "Not connected\n" : "Connected\n");

        int rv = cc::socket::send(con, "Hello", 6, 0);
        if (rv < 0)
            printf("rv < 0\n");

        else if (rv == 0)
            printf("connection closed\n");

        else
            printf("sent hello\n");

        cc::socket::close(con);
//        ::Sleep(3000);

        break;
    }
//    cc::socket::Shutdown();

    return 0;
}
