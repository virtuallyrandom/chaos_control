#ifndef __PRECOMPILED_H__
#define __PRECOMPILED_H__

#if defined( _MSC_VER )

// NEVER show these
#pragma warning(disable:4324) /* adding padding to structs/classes */
#pragma warning(disable:4710) /* function not inlined */
#pragma warning(disable:4711) /* function selected for inline expansion */
#pragma warning(disable:4820) /* N bytes padding added after data member 'soandso' */

#pragma warning(push, 1)

// don't show these for external libraries
#pragma warning(disable:4206) /* translation unit is empty */
#pragma warning(disable:4255) /* 'soandso': no function prototype given: converting '()' to '(void)' */
#pragma warning(disable:4514) /* unreferenced inline function has been removed */
#pragma warning(disable:4668) /* 'soandso' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif' */
#pragma warning(disable:4996) /* This function or variable may be unsafe, use * instead */
#pragma warning(disable:4917) /* a GUID can only be associated with a class, interface or namespace */
#pragma warning(disable:5039) /* 'soandso': pointer or reference to potentially throwing function passed to 'extern "C"' function under -EHc. Undefined behavior may occur if this function throws an exception. */

#endif /* defined( _MSC_VER ) */

#if defined( _WINDOWS )

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#pragma warning( push, 0 )

#define WIN32_LEAN_AND_MEAN
/*#define _NO_CVCONST_H*/
#define _USE_MATH_DEFINES
#define NOMINMAX

#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <DbgHelp.h>
#include <inttypes.h>

#include <commctrl.h>
#include <intrin.h>
#include <tchar.h>
#endif /* defined( _WINDOWS ) */

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <memory.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>

#pragma warning( pop, 0 )

#if defined( _MSC_VER )
	#pragma warning( pop )
#endif /* defined( _MSC_VER ) */

#if defined( _WINDOWS ) 

typedef HWND window_t;

#define PATH_MAX MAX_PATH
#define snprintf( out, outSize, fmt, ... ) _snprintf_s( out, outSize, _TRUNCATE, fmt, __VA_ARGS__ )
#define vsnprintf( out, outSize, fmt, list ) _vsnprintf_s( out, outSize, _TRUNCATE, fmt, list )

#undef alloca
#define alloca _malloca

#undef fseek
#define fseek _fseeki64_nolock

#undef ftell
#define ftell _ftelli64_nolock

typedef intptr_t ssize_t;

#else /* unknown */

#error Unsupported

#endif /* unknown */

#if defined( _WINDOWS )
#define debug_assert( x ) if ( !( x ) && IsDebuggerPresent() ) { __debugbreak(); }
#else /* other platform */
#define debug_assert( x )
#endif /* other platform */

#endif /* __PRECOMPILED_H__ */
