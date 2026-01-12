#pragma once

#if 0
#pragma warning(push, 1)

#pragma warning(disable:5039) // pointer or reference to potentially throwing function passed to 'extern "C"' function under -EHc. Undefined behavior may occur if this function throws an exception.
#pragma warning(disable:4820) // 'n' bytes padding added after data member

#pragma push_macro("WIN32_LEAN_AND_MEAN")
#define WIN32_LEAN_AND_MEAN

#pragma push_macro("NOMINMAX")
#define NOMINMAX

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#pragma pop_macro("NOMINMAX")
#pragma pop_macro("WIN32_LEAN_AND_MEAN")

#pragma warning(pop)
#endif // 0