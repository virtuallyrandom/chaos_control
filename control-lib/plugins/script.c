/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2016 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#if 0

#include "plugin.h"

#include "../list.h"

#if defined( _MSC_VER )
#pragma warning( push, 1 )
#endif /* defined( _MSC_VER ) */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#if defined( _MSC_VER )
#pragma warning( pop )
#endif /* defined( _MSC_VER ) */

#define PLUGIN_SCRIPT_CHECKSUM 0x5343524950544954 /* 'SCRIPTIT' */

typedef struct _scriptPlugin_t {
	lua_State *state;
	int pluginRef;
	int tableRef;
} scriptPlugin_t;

typedef struct _scriptData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct systemInterface_type *	systemInterface;
	list_t							scriptList;
} scriptData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static scriptData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_SCRIPT_CHECKSUM ) {
			return ( scriptData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
luaReallocate
========================
*/
static void* luaReallocate( void * const self, void *ptr, size_t originalSize, size_t newSize ) {
	scriptData_t * const me = pluginInterfaceToMe( ( struct pluginInterface_type * )self );
	void *newPtr;
	( void )originalSize;

	newPtr = me->systemInterface->reallocate( me->systemInterface, ptr, newSize );

	if ( ptr == NULL && newPtr != NULL ) {
		memset( newPtr, 0, newSize );
	}

	return newPtr;
}

/*
========================
enumFiles
========================
*/
static void enumScripts( scriptData_t * const me, void ( *cb )( scriptData_t * const, const char * const ) ) {
#if defined( _WINDOWS )
	WIN32_FIND_DATAA findData;
	HANDLE h = FindFirstFileA( "*.lua", &findData );
	if ( h != INVALID_HANDLE_VALUE ) {
		do {
			if ( cb ) {
				cb( me, findData.cFileName );
			}
		} while ( FindNextFileA( h, &findData ) );
		FindClose( h );
	}
#else /* posix */
	DIR * const dir = opendir( "./" );
	if ( dir != NULL ) {
		struct dirent *entry = readdir( dir );
		while ( entry != NULL ) {
			if ( cb ) {
				cb( me, entry->d_name );
			}
			entry = readdir( dir );
		}
		closedir( dir );
	}
#endif /* posix */
}

static lua_State * currentPluginState = NULL;

/*
========================
abortHandler
terminate the process
todo: may be better to absorb the error in exceptionHandler, disable the plugin, and continue
========================
*/
static void abortHandler( lua_State * state, lua_Debug * debugArg ) {
	( void )debugArg;
	lua_sethook( state, NULL, 0, 0);
	luaL_error( state, "interrupted!" );
}

/*
========================
exceptionHandler
stop the lua interpreter if an error occurs.
todo: may be better to absorb the error, disable the plugin, and continue
========================
*/
static void exceptionHandler( int i ) {
  signal( i, SIG_DFL );
  lua_sethook( currentPluginState, abortHandler, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1 );
}

/*
========================
errorHandler
========================
*/
static int errorHandler( lua_State * state ) {
	const int index = lua_upvalueindex( 1 );
	scriptData_t * const me = ( scriptData_t* )lua_touserdata( state, index );
	const char *msg = lua_tostring( state, 1 );
	if ( msg == NULL ) {
		if ( luaL_callmeta( state, 1, "__tostring" ) && lua_type( state, -1 ) == LUA_TSTRING) {
			me->systemInterface->logMsg( me->systemInterface, "Lua: %s", msg );
			return 1;
		}
		msg = lua_pushfstring( state, "Error object is a %s value", luaL_typename( state, 1 ) );
	}
	luaL_traceback( state, state, msg, 1 );
	me->systemInterface->logMsg( me->systemInterface, "%s", lua_tostring( state, -1 ) );
	return 1;
}

/*
========================
printHandler
========================
*/
static int printHandler( lua_State * state ) {
	const int index = lua_upvalueindex( 1 );
	scriptData_t * const me = ( scriptData_t* )lua_touserdata( state, index );
	int nargs = lua_gettop( state );
	int i;
	for ( i = 1; i <= nargs; i++ ) {
		if ( lua_isstring( state, i) ) {
			const char * msg = lua_tostring( state, i );
			me->systemInterface->logMsg( me->systemInterface, "%s", msg );
		} else {
			/* todo: handle non-string data */
		}
	}

	return 0;
}

/*
========================
callPluginFunction
========================
*/
static int callPluginFunction( scriptPlugin_t * const plugin ) {
	int err;
	int base;

	/* get the plugin table from the registry, pushing it onto the stack */
	lua_rawgeti( plugin->state, LUA_REGISTRYINDEX, plugin->tableRef );

	/* push a message handling function onto the stack */
	base = lua_gettop( plugin->state ) - 1;
	lua_rawgeti( plugin->state, LUA_REGISTRYINDEX, plugin->pluginRef );
	lua_pushcclosure( plugin->state, errorHandler, 1 );
	lua_insert( plugin->state, base );

	/* set up the signal handler to catch and handle errors */
	currentPluginState =  plugin->state; /* todo: better way? incapable of being thread safe */
	signal( SIGINT, exceptionHandler );

	err = lua_pcall( plugin->state, 1, LUA_MULTRET, base );

	/* restore the signal handler */
	signal( SIGINT, SIG_DFL );
	lua_remove( plugin->state, base );

	return err;
}

/*
========================
callPluginFunctionList
========================
*/
static void callPluginFunctionList( scriptData_t * const me, const char * const name ) {
	scriptPlugin_t *first;
	scriptPlugin_t *last;

	List_Range( me->scriptList, ( void** )&first, ( void** )&last );

	while ( first != last ) {
		lua_getglobal( first->state, name );
		if ( lua_isfunction( first->state, -1 ) ) {
			callPluginFunction( first );
		} else {
			lua_pop( first->state, -1 );
		}
		first++;
	}
}

/*
========================
onScriptFile
========================
*/
static void onScriptFile( scriptData_t * const me, const char * const filename ) {
	const char * const requiredFunctions[] = {
		"PluginCreate",
		"PluginStart",
		"PluginStop",
		"PluginGetName",
	};

	static const struct luaL_Reg printHandlerlib [] = {
		{ "print", printHandler },
		{ NULL, NULL }
	};

	scriptPlugin_t plugin;
	size_t i;
	int err;

	memset( &plugin, 0, sizeof( plugin ) );

	plugin.state = lua_newstate( luaReallocate, &me->pluginInterface );
	if ( plugin.state == NULL ) {
		me->systemInterface->logMsg( me->systemInterface, "Lua: new state failed" );
		return;
	}

	luaL_openlibs( plugin.state );

	err = luaL_loadfile( plugin.state, filename );
	if ( LUA_OK != err ) {
		me->systemInterface->logMsg( me->systemInterface, "Lua: load failed: %s", lua_tostring( plugin.state, -1 ) );
		lua_close( plugin.state );
		return;
	}

	/* prime lua, instantiating global variables and functions */
	err = lua_pcall( plugin.state, 0, LUA_MULTRET, 0 );
	if ( LUA_OK != err ) {
		me->systemInterface->logMsg( me->systemInterface, "Lua: pcall failed: %s", lua_tostring( plugin.state, -1 ) );
		lua_close( plugin.state );
		return;
	}

	/* verify each of the functions required for a plugin to exist */
	for ( i = 0; i < sizeof( requiredFunctions ) / sizeof( *requiredFunctions ); i++ ) {
		int type;

		lua_getglobal( plugin.state, requiredFunctions[ i ] );
		type = lua_type( plugin.state, -1 );
		if ( LUA_TFUNCTION != type ) {
			lua_close( plugin.state );
			return;
		}

		lua_pop( plugin.state, -1 );
	}

	/* add a ref to the plugin so it can be accessed from anywhere the state goes */
	lua_pushlightuserdata( plugin.state, me );
	plugin.pluginRef = luaL_ref( plugin.state,LUA_REGISTRYINDEX );

	/* create a global table to contain data over successive calls */
	lua_createtable( plugin.state, 0, 0 );
	plugin.tableRef = luaL_ref( plugin.state, LUA_REGISTRYINDEX );

	/* replace the print function (see lbaselib.c) */
	lua_pushglobaltable( plugin.state );
	lua_rawgeti( plugin.state, LUA_REGISTRYINDEX, plugin.pluginRef );
	luaL_setfuncs( plugin.state, printHandlerlib, 1 );

	List_Append( me->scriptList, &plugin );

	lua_getglobal( plugin.state, "PluginCreate" );
	callPluginFunction( &plugin );

	lua_getglobal( plugin.state, "PluginStart" );
	callPluginFunction( &plugin );
}

/*
========================
scriptStart
========================
*/
static void scriptStart( struct pluginInterface_type * const self ) {
	scriptData_t * const me = pluginInterfaceToMe( self );
	( void )me;

	me->scriptList = List_Create( sizeof( scriptPlugin_t ), 8 );

	enumScripts( me, onScriptFile );
}

/*
========================
scriptStop
========================
*/
static void scriptStop( struct pluginInterface_type * const self ) {
	scriptData_t * const me = pluginInterfaceToMe( self );
	scriptPlugin_t *first;
	scriptPlugin_t *last;

	List_Range( me->scriptList, ( void** )&first, ( void** )&last );

	while ( first != last ) {
		lua_getglobal( first->state, "PluginStop" );
		callPluginFunction( first );

		luaL_unref( first->state, LUA_REGISTRYINDEX, first->tableRef );
		luaL_unref( first->state, LUA_REGISTRYINDEX, first->pluginRef );

		lua_close( first->state );

		first++;
	}

	List_Destroy( me->scriptList );
}

/*
========================
scriptUpdate
========================
*/
static void scriptUpdate( struct pluginInterface_type * const self ) {
	scriptData_t * const me = pluginInterfaceToMe( self );
	callPluginFunctionList( me, "PluginUpdate" );
}

/*
========================
scriptRender
========================
*/
static void scriptRender( struct pluginInterface_type * const self ) {
	scriptData_t * const me = pluginInterfaceToMe( self );
	callPluginFunctionList( me, "PluginRender" );
}

/*
========================
scriptReport
========================
*/
static void scriptReport( struct pluginInterface_type * const self, FILE * const fh ) {
	scriptData_t * const me = pluginInterfaceToMe( self );
	( void )me;
	( void )fh;
}

/*
========================
scriptGetName
========================
*/
static void* scriptGetPrivateInterface( struct pluginInterface_type * const self ) {
	( void )self;
	return NULL;
}

/*
========================
scriptGetName
========================
*/
static const char* scriptGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return "Script";
}

/*
========================
Script_Create
========================
*/
struct pluginInterface_type * Script_Create( struct systemInterface_type * const sys ) {
	scriptData_t * const me = ( scriptData_t* )sys->allocate( sys, sizeof( scriptData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( scriptData_t ) );

	me->checksum = PLUGIN_SCRIPT_CHECKSUM;

	me->pluginInterface.start				= scriptStart;
	me->pluginInterface.stop				= scriptStop;
	me->pluginInterface.update				= scriptUpdate;
	me->pluginInterface.render				= scriptRender;
	me->pluginInterface.resize				= scriptResize;
	me->pluginInterface.report				= scriptReport;
	me->pluginInterface.getName				= scriptGetName;
	me->pluginInterface.getPrivateInterface	= scriptGetPrivateInterface;
	me->pluginInterface.getName				= scriptGetName;

	me->systemInterface = sys;

	return &me->pluginInterface;
}

#endif // 0
