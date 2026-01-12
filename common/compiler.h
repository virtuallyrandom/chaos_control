#pragma once

#include <common/assert.h>

template<typename Type, size_t Count>
size_t countof(Type const (&)[Count])
{
    return Count;
}

template< typename To, typename From >
To truncate_cast(From const& value)
{
    To to = static_cast<To>(value);
    assert(static_cast<From>(to) == value);
    return to;
}

#define decl_align(n) __declspec(align(n))

#define compiler_disable_copymove(n) \
    n(n const&) = delete;            \
    n(n&&) = delete;                 \
    n& operator=(n const&) = delete; \
    n& operator=(n&&) = delete;

#define compiler_push_disable_implicit_padding()\
    _Pragma("warning(disable:4324)")\
    _Pragma("warning(disable:4820)")

#define compiler_pop_disable_implicit_padding()\
    _Pragma("warning(default:4820)")\
    _Pragma("warning(default:4324)")

#if defined( _MSC_VER )
    #define PRINTF_FN(fmtIndex, argIndex) 
    #define PRINTF_ARG _Printf_format_string_
#elif defined( __clang__ )
    #define PRINTF_FN(fmtIndex, argIndex) __attribute__((format(printf, (fmtIndex) + 1, (argIndex) + 1)))
    #define PRINTF_ARG
#else
    #error Unknown compiler
#endif

#define compiler_unreachable

// Helper messages to output data at build time
#define CMP_MAKE_STR( x )  #x
#define CMP_MAKE_STR2( x ) CMP_MAKE_STR( x )
#define CMP_MAKE_MSG( title, x ) __pragma( message( __FILE__ "(" CMP_MAKE_STR2( __LINE__ ) ") : " title ": " x) )

#define BUILDMSG_TODO( x ) CMP_MAKE_MSG( "ToDo", #x )
#define BUILDMSG_FIXME( x ) CMP_MAKE_MSG( "FixMe", #x )
#define BUILDMSG_THINK( x ) CMP_MAKE_MSG( "Thinking", #x )
#define BUILDMSG_WARN( x ) CMP_MAKE_MSG( "warning", #x )
#define BUILDMSG_ERROR( x ) CMP_MAKE_MSG( "error", #x )

#pragma warning(disable:5045) // Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified