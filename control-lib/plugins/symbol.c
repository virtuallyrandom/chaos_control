/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "../avltree.h"
#include "../list.h"
#include "../stringTable.h"
#include "platform.h"
#include "plugin.h"
#include "symbol.h"
#include "symbol_pdb.h"

#pragma warning( push, 1 )
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma warning( pop )

#define PLUGIN_SYMBOL_CHECKSUM 0x53594D424f4c2020 /* 'SYMBOL  ' */

#define LIST_GROWTH_SIZE 512
#define MAX_MODULES 128

typedef struct _stringEntryLoadFormat_t {
	uint32_t	uid;
	uint32_t	padding;
	stringId_t	stringId;
} stringEntryLoadFormat_t;

typedef struct _symbolLoadFormat_t {
	uintptr_t	addr;
	uint64_t	size;
	uint32_t	line;
	uint32_t	column;
	uint32_t	fileUID;
	uint32_t	functionUID;
	const char*	file;
	const char*	function;
} symbolLoadFormat_t;

typedef struct _symbol_t {
	uint64_t	addr;
	uint64_t	size;
	uint32_t	line;
	uint32_t	column;
	uint32_t	fileOffset;
	uint32_t	functionOffset;
} symbol_t;

typedef struct _symbolTable_t {
	uint64_t	offsetToSymbolAddr; /* array of uint64_t's that are stored in sort order */
	uint64_t	sizeofOfSymbolAddr;
	uint64_t	offsetToSymbolData; /* array of symbol_t that is stored in symbol addr order */
	uint64_t	sizeOfSymbolData;
	uint64_t	offsetToStringTable; /* multistring */
} symbolTable_t;

typedef struct _moduleInfo_t {
	uint64_t				baseAddr;
	uint64_t				imageSize;
	uint64_t				codeBase;
	uint64_t				codeSize;
	struct _symbolData_t *	symbolData;
	enum platformId_type	platform;
	char					path[ MAX_PATH ];
	volatile int32_t		loadInProgress;
	int32_t 				hasSymbols;
} moduleInfo_t;

typedef struct _symbolData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	symbolInterface_t				symbolInterface;
	struct systemInterface_type *	systemInterface;
	CRITICAL_SECTION				lock;
	HANDLE							thread;
	struct stringTable_type *		stringTable;
	list_t							listFile;
	list_t							listFunction;
	symbolTable_t*					symbolTable;
	symbolLoadFormat_t*				symbols;
	uintptr_t*						symbolSearch;
	size_t							symbolCount;
	size_t							symbolEnd;
	moduleInfo_t					module[ MAX_MODULES ];
	size_t							moduleCount;
	int32_t							cancelLoad;
	uint32_t						padding;
	struct platformInfo_type		platformInfo;
	struct buildInfo_type			buildInfo;
} symbolData_t;

const char PLUGIN_NAME_SYMBOL[] = "Symbol";

static const uint16_t SYSTEM_SYSTEM = 0;
static const uint16_t PACKET_BASEADDR__DEPRECATED = 10;
static const uint16_t PACKET_BASEADDR = 12;

/*
========================
pluginInterfaceToMe
========================
*/
static symbolData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_SYMBOL_CHECKSUM ) {
			return ( symbolData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
symbolInterfaceToMe
========================
*/
static symbolData_t* symbolInterfaceToMe( symbolInterface_t * const iface ) {
	const uint8_t * const ptrToSymbolInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToSymbolInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_SYMBOL_CHECKSUM ) {
		return ( symbolData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
symbolSort
========================
*/
static int symbolSort( const void * const a_, const void * const b_ ) {
	const symbolLoadFormat_t * const a = ( const symbolLoadFormat_t* )a_;
	const symbolLoadFormat_t * const b = ( const symbolLoadFormat_t* )b_;
	if ( a->addr < b->addr ) {
		return -1;
	}
	if ( a->addr > b->addr ) {
		return 1;
	}
	return ( int )( ( intptr_t )a - ( intptr_t )b );
}

/*
========================
lock
========================
*/
static void lock( symbolData_t * const me ) {
	EnterCriticalSection( &me->lock );
}

/*
========================
tryLock
========================
*/
static int tryLock( symbolData_t * const me ) {
	return ( TryEnterCriticalSection( &me->lock ) ? 1 : 0 );
}

/*
========================
unlock
========================
*/
static void unlock( symbolData_t * const me ) {
	LeaveCriticalSection( &me->lock );
}

/*
========================
stringSort
========================
*/
static int stringSort( const void * const a_, const void * const b_ ) {
	const stringEntryLoadFormat_t * const a = ( const stringEntryLoadFormat_t* )a_;
	const stringEntryLoadFormat_t * const b = ( const stringEntryLoadFormat_t* )b_;
	if ( a->uid < b->uid ) {
		return -1;
	}
	if ( a->uid > b->uid ) {
		return 1;
	}
	return ( int )( ( intptr_t )a - ( intptr_t )b );
}

/*
========================
countSymbol
========================
*/
static int32_t countSymbol( void * const param, const symbolInfo_t * const symInfo ) {
	symbolData_t * const me = ( symbolData_t* )param;
	( void )symInfo;
	me->symbolCount++;
	return me->cancelLoad ? 0 : 1;
}

/*
========================
enumSymbol
========================
*/
static int32_t enumSymbol( void * const param, const symbolInfo_t * const symInfo ) {
	symbolData_t * const me = ( symbolData_t* )param;
	symbolLoadFormat_t * const entry = me->symbols + me->symbolEnd++;
	entry->addr = symInfo->addr;
	entry->size = symInfo->size;
	entry->line = symInfo->line;
	entry->column = symInfo->column;
	entry->fileUID = symInfo->fileUID;
	entry->functionUID = symInfo->functionUID;
	entry->file = 0;
	entry->function = 0;
	return me->cancelLoad ? 0 : 1;
}

/*
========================
enumFleString
========================
*/
static int32_t enumFileString( void * const param, const symbolString_t * const symString ) {
	symbolData_t * const me = ( symbolData_t* )param;
	stringEntryLoadFormat_t entry;
	entry.uid = symString->uid;
	lock( me );
	entry.stringId = StringTable_Insert( me->stringTable, symString->value );
	List_Append( me->listFile, &entry );
	unlock( me );
	return me->cancelLoad ? 0 : 1;
}

/*
========================
enumFunctionString
========================
*/
static int32_t enumFunctionString( void * const param, const symbolString_t * const symString ) {
	symbolData_t * const me = ( symbolData_t* )param;
	stringEntryLoadFormat_t entry;
	entry.uid = symString->uid;
	lock( me );
	entry.stringId = StringTable_Insert( me->stringTable, symString->value );
	List_Append( me->listFunction, &entry );
	unlock( me );
	return me->cancelLoad ? 0 : 1;
}

/*
========================
findInTable
========================
*/
static const char* findInTable(	struct stringTable_type * const stringTable,
								list_t const list,
								const uint32_t uid ) {
	size_t low = 0;
	size_t high = 0;
	stringEntryLoadFormat_t *obj;

	high = List_GetNum( list );

	if ( high == 0 ) {
		return 0;
	}

	for ( ;; ) {
		size_t pivot;
		size_t range = high - low;

		if ( range == 1 ) {
			break;
		}

		pivot = low + range / 2;

		obj = ( stringEntryLoadFormat_t* )List_GetObject( list, pivot );

		if ( obj->uid < uid ) {
			low = pivot;
		} else if ( obj->uid > uid ) {
			high = pivot;
		} else {
			return StringTable_Get( stringTable, obj->stringId );
		}
	}

	obj = ( stringEntryLoadFormat_t* )List_GetObject( list, low );

	if ( obj->uid != uid ) {
		return 0;
	}

	return StringTable_Get( stringTable, obj->stringId );
}

/*
========================
loadPDB
========================
*/
static int loadPDB( symbolData_t * const me, moduleInfo_t * const moduleInfo ) {
	symbolPdb_t pdb;
	int ok = 0;

	char pdbPath[ MAX_PATH ];

	if ( moduleInfo->path[ 0 ] == 0 ) {
		strcpy( pdbPath, "GHOST.pdb" );
	} else {
		strcpy( pdbPath, moduleInfo->path );
		{
			char * ptr;
			ptr = pdbPath + strlen( pdbPath ) - 1;
			while ( *ptr != '.' && ptr > pdbPath ) {
				ptr--;
			}
			memcpy( ptr, ".pdb", 5 );
		}
	}

	if ( moduleInfo->loadInProgress ) {
		while ( moduleInfo->loadInProgress ) {
			Sleep( 10 );
		}
	}

	me->cancelLoad = 0;

	moduleInfo->loadInProgress = 1;
	//guiProgressValue = 0;

#if 0
	List_Destroy( me->listFile );
	me->listFile = NULL;

	List_Destroy( me->listFunction );
	me->listFunction = NULL;

	if ( me->symbols ) {
		me->systemInterface->deallocate( me->systemInterface, me->symbols );
		me->symbols = NULL;
	}
	if ( me->symbolSearch ) {
		me->systemInterface->deallocate( me->systemInterface, me->symbolSearch );
		me->symbolSearch = NULL;
	}
	me->symbolCount = 0;
	me->symbolEnd = 0;
#endif // 0

	pdb = SymbolPdb_Create();

	if ( SymbolPdb_Load( pdb, pdbPath ) ) {
		ULONGLONG time = 0;

		size_t i;
//		int atStep = 0;
//		int totalSteps = 13;

		me->systemInterface->logMsg( me->systemInterface, "Loading symbol from path: %s", pdbPath );

		/* count and fill the file string table */
		time = GetTickCount64();
		me->systemInterface->logMsg( me->systemInterface, "SymbolPdb_EnumerateFiles: " );
		SymbolPdb_EnumerateFiles( pdb, enumFileString, me );
		me->systemInterface->logMsg( me->systemInterface, "%.3fs\n", ( GetTickCount64() - time ) / 1000.0f );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		/* todo: sort, in a thread or a job */
		time = GetTickCount64();
		me->systemInterface->logMsg( me->systemInterface, "File Sort: " );
		lock( me );
		List_Sort( me->listFile, stringSort );
		unlock( me );
		me->systemInterface->logMsg( me->systemInterface, "%.3fs, %" PRIu64 " objects\n", ( GetTickCount64() - time ) / 1000.0f, List_GetNum( me->listFile ) );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		/* count and fill the function string table */
		time = GetTickCount64();
		me->systemInterface->logMsg( me->systemInterface, "SymbolPdb_EnumerateFunctions: " );
		SymbolPdb_EnumerateFunctions( pdb, enumFunctionString, me );
		me->systemInterface->logMsg( me->systemInterface, "%.3fs\n", ( GetTickCount64() - time ) / 1000.0f );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		/* todo: sort, in a thread or a job */
		time = GetTickCount64();
		me->systemInterface->logMsg( me->systemInterface, "Function Sort: " );
		lock( me );
		List_Sort( me->listFunction, stringSort );
		unlock( me );
		me->systemInterface->logMsg( me->systemInterface, "%.3fs, %" PRIu64 " objects\n", ( GetTickCount64() - time ) / 1000.0f, List_GetNum( me->listFunction ) );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		/* load the symbols */
		time = GetTickCount64();
		me->systemInterface->logMsg( me->systemInterface, "SymbolPdb_EnumerateSymbols: " );
		lock( me );
		me->symbolCount += SymbolPdb_EnumerateSymbols( pdb, countSymbol, me );
		unlock( me );
		me->systemInterface->logMsg( me->systemInterface, "%.3fs\n", ( GetTickCount64() - time ) / 1000.0f );
		//guiProgressValue = atStep++ / ( float )totalSteps;
		lock( me );
		me->symbols = ( symbolLoadFormat_t* )me->systemInterface->reallocate( me->systemInterface, me->symbols, me->symbolCount * sizeof( symbolLoadFormat_t ) );
		me->symbolSearch = ( uintptr_t* )me->systemInterface->reallocate( me->systemInterface, me->symbolSearch, me->symbolCount * sizeof( uintptr_t ) );
		time = GetTickCount64();
		SymbolPdb_EnumerateSymbols( pdb, enumSymbol, me );
		unlock( me );
		me->systemInterface->logMsg( me->systemInterface, "%.3fs\n", ( GetTickCount64() - time ) / 1000.0f );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		/* todo: sort, in a thread or a job */
		time = GetTickCount64();
		lock( me );
		qsort( me->symbols, me->symbolEnd, sizeof( symbolLoadFormat_t ), symbolSort );
		unlock( me );
		me->systemInterface->logMsg( me->systemInterface, "symbol sort: %.3fs, %" PRIu64 " objects\n", ( GetTickCount64() - time ) / 1000.0f, me->symbolEnd );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		/* copy the symbol addresses to the search table for faster searches */
		lock( me );
		for ( i = 0; i < me->symbolEnd; ++i ) {
			me->symbolSearch[ i ] = me->symbols[ i ].addr;
		}
		unlock( me );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		/* map files from string table to symbol info */
		lock( me );
		for ( i = 0; i < me->symbolEnd; ++i ) {
			me->symbols[ i ].file = findInTable( me->stringTable, me->listFile, me->symbols[ i ].fileUID );
		}
		unlock( me );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		/* map functions from string table to symbol info */
		lock( me );
		for ( i = 0; i < me->symbolEnd; ++i ) {
			me->symbols[ i ].function = findInTable( me->stringTable, me->listFunction, me->symbols[ i ].functionUID );
		}
		unlock( me );
		//guiProgressValue = atStep++ / ( float )totalSteps;

		moduleInfo->hasSymbols = 1;
		ok = 1;
	}

	SymbolPdb_Destroy( pdb );

	//guiProgressValue = 1.0f;

	/*
		don't set this until the loads are completed and sorted, otherwise finds will run
		against unsorted data
	*/
	moduleInfo->loadInProgress = 0;

	return ok;
}

/*
========================
loadSymbolThread
========================
*/
DWORD __stdcall loadSymbolThread( void * param ) {
	symbolData_t * const me = ( symbolData_t * )param;

	size_t index = 0;

	while ( me->cancelLoad == 0 ) {
		if ( index < me->moduleCount ) {
			moduleInfo_t * const moduleInfo = &me->module[ index++ ];
//			switch ( me->platformInfo.platform ) {
//				case PLATFORM_X64:
//				case PLATFORM_DURANGO: {
					loadPDB( me, moduleInfo );
//			   } break;

//				case PLATFORM_ORBIS:
//				case PLATFORM_LINUX:
//				case PLATFORM_YETI:
					/* TODO: convert dwarf to common format */
//					break;

//				default:
//					break;
//			}
		} else {
			Sleep( 10 );
		}
	}

	return 0;
}

/*
========================
loadSymbols
========================
*/
static void loadSymbols( symbolData_t * const me, moduleInfo_t * const moduleInfo ) {
	( void )moduleInfo;
	if ( me->thread == INVALID_HANDLE_VALUE ) {
		me->thread = CreateThread( NULL, 0, loadSymbolThread, me, 0, NULL );
	} else {
		/* todo: wake semaphore */
	}
}

/*
========================
onBaseAddrDeprecated
========================
*/
static void onBaseAddrDeprecated( void * const param, const struct packetHeader_type * const hdr ) {
	struct pkt_t {
		struct packetHeader_type header;
		uint64_t baseAddr;
		uint64_t imageSize;
		uint64_t codeBase;
		uint64_t codeSize;
	} *pkt = ( struct pkt_t * )hdr;

	symbolData_t * const me = ( symbolData_t* )param;
	moduleInfo_t * const moduleInfo = &me->module[ me->moduleCount++ ];
	size_t stringLength = pkt->header.size - sizeof( struct pkt_t );
	stringLength = min( stringLength, sizeof( moduleInfo->path ) - 1 );
	memcpy( moduleInfo->path, pkt + 1, stringLength );
	moduleInfo->path[ stringLength ] = 0;
	moduleInfo->baseAddr = pkt->baseAddr;
	moduleInfo->imageSize = 0x7fffffffffff;
	moduleInfo->codeBase = pkt->baseAddr;
	moduleInfo->codeSize = 0x7fffffffffff;
	
	loadSymbols( me, moduleInfo );
}

/*
========================
onBaseAddr
========================
*/
static void onBaseAddr( void * const param, const struct packetHeader_type * const hdr ) {
	struct pkt_t {
		struct packetHeader_type header;
		uint64_t baseAddr;
		uint64_t imageSize;
		uint64_t codeBase;
		uint64_t codeSize;
	} *pkt = ( struct pkt_t * )hdr;

	symbolData_t * const me = ( symbolData_t* )param;
	moduleInfo_t * const moduleInfo = &me->module[ me->moduleCount++ ];
	size_t stringLength = pkt->header.size - sizeof( struct pkt_t );
	stringLength = min( stringLength, sizeof( moduleInfo->path ) - 1 );
	memcpy( moduleInfo->path, pkt + 1, stringLength );
	moduleInfo->path[ stringLength ] = 0;
	moduleInfo->baseAddr = pkt->baseAddr;
	moduleInfo->imageSize = pkt->imageSize;
	moduleInfo->codeBase = pkt->codeBase;
	moduleInfo->codeSize = pkt->codeSize;
	
	loadSymbols( me, moduleInfo );
}

/*
========================
onPlatformInfo
========================
*/
static void onPlatformInfo( void * const param, const struct platformInfo_type * const data ) {
	symbolData_t * const me = ( symbolData_t* )param;
	memcpy( &me->platformInfo, data, sizeof( struct platformInfo_type ) );
//	tryLoadSymbols( me );
}

/*
========================
onBuildInfo
========================
*/
static void onBuildInfo( void * const param, const struct buildInfo_type * const data ) {
	symbolData_t * const me = ( symbolData_t* )param;
	memcpy( &me->buildInfo, data, sizeof( struct buildInfo_type ) );
//	tryLoadSymbols( me );
}

/*
========================
myFind
========================
*/
extern int loadElf( const char * const path, const uintptr_t addr, char* callstack_name );
static int myFind(	symbolInterface_t * const self,
					const uint64_t srcAddr,
					const char ** const name,
					const char ** const file,
					uint32_t * const line ) {
	symbolData_t * const me = symbolInterfaceToMe( self );
	size_t low = 0;
	size_t high = 0;
	uintptr_t addr = ( uintptr_t )srcAddr;

	if ( me == NULL ) {
		return 0;
	}

	if ( !tryLock( me ) ) {
		return 0;
	}

	for ( size_t i = 0; i < me->moduleCount; ++i ) {
		moduleInfo_t * const moduleInfo = &me->module[ i ];

		/* TODO: merge these searches together */
		if ( moduleInfo->loadInProgress  || moduleInfo->hasSymbols == 0 ) {
			continue;
		}

		if ( srcAddr < moduleInfo->baseAddr + moduleInfo->codeBase || srcAddr >= moduleInfo->baseAddr + moduleInfo->codeBase + moduleInfo->codeSize ) {
			continue;
		}

		addr = srcAddr - moduleInfo->baseAddr;

#if 0
		if ( me->systemInterface->getPlatform( me->systemInterface ) == PLATFORM_ORBIS ) {
			char result[ PATH_MAX ];
			if ( 0 == loadElf( "DOOM.elf", addr, result ) ) {
				if ( 0 == loadElf( "Zion.elf", addr, result ) ) {
					if ( 0 == loadElf( "eboot.bin", addr, result ) ) {
						strcpy( result, "Unknown" );
					}
				}
			}

			if ( name != NULL ) {
				*name = me->systemInterface->addString( me->systemInterface, result );
			}
			if ( file != NULL ) {
				*file = "";
			}
			if ( line != NULL ) {
				*line = 0;
			}
			return 1;
		}
#endif // 0

		high = me->symbolEnd;

		if ( high == 0 ) {
			continue;
		}

		for (;;) {
			size_t pivot;
			size_t range = high - low;

			if ( range == 1 ) {
				break;
			}

			pivot = low + range / 2;

			if ( me->symbolSearch[ pivot ] < addr ) {
				low = pivot;
			} else {
				high = pivot;
			}
		}

		/*
			since addresses are sorted linearly, anything *after* the lower address bounds is
			actually located within the lower function call, so we simply insert and use that
			stack/function value.
		*/
		if ( me->symbolSearch[ low ] > addr ) {
			continue;
		}

		if ( name ) {
			*name = me->symbols[ low ].function;
		}

		if ( file ) {
			*file = me->symbols[ low ].file;
		}

		if ( line ) {
			*line = me->symbols[ low ].line;
		}

		unlock( me );
		return 1;
	}

	unlock( me );

	return 0;
}

/*
========================
myDestroy
========================
*/
static void myDestroy( symbolData_t * const me ) {
	me->cancelLoad = 1;

	if ( me->thread != INVALID_HANDLE_VALUE ) {
		WaitForSingleObject( me->thread, INFINITE );
		CloseHandle( me->thread );
		me->thread = INVALID_HANDLE_VALUE;
	}

	me->systemInterface->deallocate( me->systemInterface, me->symbolTable );
	me->symbolTable = NULL;

	me->systemInterface->deallocate( me->systemInterface, me->symbols );
	me->symbols = NULL;

	me->systemInterface->deallocate( me->systemInterface, me->symbolSearch );
	me->symbolSearch = NULL;

	me->symbolCount = 0;
	me->symbolEnd = 0;

	List_Destroy( me->listFile );
	me->listFile = NULL;

	List_Destroy( me->listFunction );
	me->listFunction = NULL;

	StringTable_Clear( me->stringTable );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	symbolData_t * const me = pluginInterfaceToMe( self );
	struct platformInterface_type * platformInterface;

	if ( me == NULL ) {
		return;
	}

	InitializeCriticalSection( &me->lock );

	me->listFile = List_Create( sizeof( stringEntryLoadFormat_t ), LIST_GROWTH_SIZE );
	me->listFunction = List_Create( sizeof( stringEntryLoadFormat_t ), LIST_GROWTH_SIZE );
	me->stringTable = StringTable_Create( me->systemInterface );

	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_BASEADDR, onBaseAddr, me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_BASEADDR__DEPRECATED, onBaseAddrDeprecated, me );

	platformInterface = ( struct platformInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_PLATFORM );
	if ( platformInterface != NULL ) {
		platformInterface->registerOnBuildInfo( platformInterface, onBuildInfo, me );
		platformInterface->registerOnPlatformInfo( platformInterface, onPlatformInfo, me );
	}
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	symbolData_t * const me = pluginInterfaceToMe( self );
	struct platformInterface_type * platformInterface;

	if ( me == NULL ) {
		return;
	}

	me->cancelLoad = 1;

	if ( me->thread != NULL ) {
		WaitForSingleObject( me->thread, 1000 );
		CloseHandle( me->thread );
		me->thread = INVALID_HANDLE_VALUE;
	}

	platformInterface = ( struct platformInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_PLATFORM );
	if ( platformInterface != NULL ) {
		platformInterface->unregisterOnPlatformInfo( platformInterface, onPlatformInfo, me );
		platformInterface->unregisterOnBuildInfo( platformInterface, onBuildInfo, me );
	}

	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_BASEADDR__DEPRECATED, onBaseAddrDeprecated, me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_BASEADDR, onBaseAddr, me );

	DeleteCriticalSection( &me->lock );

	myDestroy( me );

	StringTable_Destroy( me->stringTable );
	me->stringTable = NULL;

	List_Destroy( me->listFile );
	me->listFile = NULL;

	List_Destroy( me->listFunction );
	me->listFunction = NULL;
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_SYMBOL;
}

/*
========================
SymbolGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	symbolData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return &me->symbolInterface;
}

/*
========================
MemoryGetPluginInterface
========================
*/
struct pluginInterface_type * Symbol_Create( struct systemInterface_type * const sys ) {
	symbolData_t * const me = ( symbolData_t* )sys->allocate( sys, sizeof( symbolData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( symbolData_t ) );

	me->checksum = PLUGIN_SYMBOL_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->symbolInterface.find = myFind;

	me->systemInterface = sys;
	me->thread = INVALID_HANDLE_VALUE;

	me->platformInfo.platform = UINT8_MAX;
	me->platformInfo.binaryType = UINT8_MAX;

	return &me->pluginInterface;
}
