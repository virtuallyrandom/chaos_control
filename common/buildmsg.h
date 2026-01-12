#pragma once

// Helper messages to output data at build time
#define CMP_MAKE_STR( x )  #x
#define CMP_MAKE_STR2( x ) CMP_MAKE_STR( x )
#define CMP_MAKE_MSG( title, x ) __pragma( message( __FILE__ "(" CMP_MAKE_STR2( __LINE__ ) ") : " title ": " x) )

#define BUILDMSG_TODO( x ) CMP_MAKE_MSG( "ToDo", #x )
#define BUILDMSG_FIXME( x ) CMP_MAKE_MSG( "FixMe", #x )
#define BUILDMSG_THINK( x ) CMP_MAKE_MSG( "Thinking", #x )
#define BUILDMSG_WARN( x ) CMP_MAKE_MSG( "warning", #x )
#define BUILDMSG_ERROR( x ) CMP_MAKE_MSG( "error", #x )
