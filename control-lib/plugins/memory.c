/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "../avltree.h"
#include "../thread.h"
#include "memory.h"
#include "plugin.h"
#include "symbol.h"

#define PLUGIN_MEMORY_CHECKSUM 0x204d454d4f525920 /* ' MEMORY ' */

#define MAX_CALLBACKS	32
#define MAX_HEAPS		8
#define MAX_TAGS		256

#define SYSTEM_MEMORY 1

#define PACKET_HEAP_CREATE		8
#define PACKET_HEAP_DESTROY		2
#define PACKET_HEAP_RESET		3
#define PACKET_MEM_ALLOC		4
#define PACKET_MEM_FREE			5
#define PACKET_MEM_TAG			6
#define PACKET_MEM_FILELINE		7
#define PACKET_MEM_CALLSTACK	9

#define PACKET_HEAP_CREATE_DEPRECATED1	1

/*
========================
Memory Packets
========================
*/
typedef struct _heapData_t {
	const char*	name;
	uint64_t	heapID;
	uint64_t	addrRangeBegin;
	uint64_t	addrRangeEnd;
	uint64_t	heapStart;
	uint64_t	heapSize;
	uint32_t	requestedBytes;
	uint32_t	actualBytes;
	avlTree_t	allocTable;
} heapData_t;

typedef struct _tagInfo_t {
	const char* name;
} tagInfo_t;

typedef struct _heapCallbackInfo_t {
	onHeapCallback	cb;
	void*			param;
} heapCallbackInfo_t;

typedef struct _blockCallbackInfo_t {
	onMemBlockCallback	cb;
	void*				param;
} blockCallbackInfo_t;

typedef struct _tagCallbackInfo_t {
	onMemTagCallback	cb;
	void*				param;
} tagCallbackInfo_t;

const char PLUGIN_NAME_MEMORY[] = "Memory";

typedef struct _memoryData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct memoryInterface_type		memoryInterface;
	struct systemInterface_type *	systemInterface;
	size_t							heapCount;
	heapData_t						heap[ MAX_HEAPS ];
	tagInfo_t						tags[ MAX_TAGS ];
	heapCallbackInfo_t				onHeapCreateCB[ MAX_CALLBACKS ];
	size_t							onHeapCreateCount;
	heapCallbackInfo_t				onHeapDestroyCB[ MAX_CALLBACKS ];
	size_t							onHeapDestroyCount;
	heapCallbackInfo_t				onHeapResetCB[ MAX_CALLBACKS ];
	size_t							onHeapResetCount;
	blockCallbackInfo_t				onMemAllocCB[ MAX_CALLBACKS ];
	size_t							onMemAllocCount;
	blockCallbackInfo_t				onMemFreeCB[ MAX_CALLBACKS ];
	size_t							onMemFreeCount;
	tagCallbackInfo_t				onMemTagCB[ MAX_CALLBACKS ];
	size_t							onMemTagCount;
	blockCallbackInfo_t				onMemFileLineCB[ MAX_CALLBACKS ];
	size_t							onMemFileLineCount;
	blockCallbackInfo_t				onMemCallstackCB[ MAX_CALLBACKS ];
	size_t							onMemCallstackCount;
	heapData_t						overflowHeap;
} memoryData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static memoryData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_MEMORY_CHECKSUM ) {
			return ( memoryData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
memoryInterfaceToMe
========================
*/
static memoryData_t* memoryInterfaceToMe( struct memoryInterface_type * const iface ) {
	const uint8_t * const ptrToMemoryInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToMemoryInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_MEMORY_CHECKSUM ) {
		return ( memoryData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
findHeap
========================
*/
static heapData_t* findHeap( memoryData_t * const me, uint64_t heapID ) {
	size_t i;

	for ( i = 0; i < me->heapCount; ++i ) {
		heapData_t * const heap = me->heap + i;
		if ( heap->heapID == heapID ) {
			return heap;
		}
	}

	if ( me->heapCount != MAX_HEAPS ) {
		heapData_t * const heap = me->heap + me->heapCount++;
		memset( heap, 0, sizeof( heapData_t ) );
		heap->heapID = heapID;
		heap->allocTable = AVLTreeCreate( me->systemInterface, sizeof( uint64_t ) );
		return heap;
	}

	/* todo: warn/error/something */
	if ( me->overflowHeap.allocTable == NULL ) {
		me->overflowHeap.allocTable = AVLTreeCreate( me->systemInterface, sizeof( uint64_t ) );
	}
	return &me->overflowHeap;
}

/*
========================
heapFreeCallback
========================
*/
static void heapFreeCallback( memoryData_t * const me, const void * const key, void * const value ) {
	struct allocInfo_type * const info = ( struct allocInfo_type * )value;
	size_t i;

	( void )key;

	for ( i = 0; i < me->onMemFreeCount; ++i ) {
		blockCallbackInfo_t * const cb = me->onMemFreeCB + i;
		if ( cb->cb != NULL ) {
			cb->cb( cb->param, info );
		}
	}

	me->systemInterface->deallocate( me->systemInterface, info->callstack );
	me->systemInterface->deallocate( me->systemInterface, info );
}

/*
========================
walkCallback
========================
*/
static void walkCallback( blockCallbackInfo_t * const cb, const void * const key, void * const value ) {
	struct allocInfo_type * const obj = ( struct allocInfo_type * )value;
	( void )key;
	if ( cb->cb != NULL ) {
		cb->cb( cb->param, obj );
	}
}

/*
========================
processHeapNotification
========================
*/
static void processHeapNotification(	heapCallbackInfo_t * const list,
										const size_t count,
										const heapData_t * const heap ) {
	size_t i;
	for ( i = 0; i < count; ++i ) {
		heapCallbackInfo_t * const info = list + i;
		if ( info->cb != NULL ) {
			info->cb( info->param, ( const struct heapInfo_type * )heap );
		}
	}
}

/*
========================
processBlockNotification
========================
*/
static void processBlockNotification(	blockCallbackInfo_t * const list,
										const size_t count,
										const struct allocInfo_type * const block ) {
	size_t i;
	for ( i = 0; i < count; ++i ) {
		blockCallbackInfo_t * const info = list + i;
		if ( info->cb != NULL ) {
			info->cb( info->param, block );
		}
	}
}

/*
========================
registerHeapCallback
========================
*/
static void registerHeapCallback(	heapCallbackInfo_t * const list,
									size_t * const count,
									onHeapCallback cb,
									void * const param ) {
	size_t i;

	for ( i = 0; i < *count; ++i ) {
		heapCallbackInfo_t * const info = list + i;
		if ( info->cb == 0 ) {
			info->cb = cb;
			info->param = param;
			return;
		}
	}

	if ( *count != MAX_CALLBACKS ) {
		heapCallbackInfo_t * const info = list + ( *count )++;
		info->cb = cb;
		info->param = param;
	}
}

/*
========================
unregisterHeapCallback
========================
*/
static void unregisterHeapCallback(	heapCallbackInfo_t * const list,
									const size_t count,
									onHeapCallback cb,
									void * const param ) {
	size_t i;
	for ( i = 0; i < count; ++i ) {
		heapCallbackInfo_t * const info = list + i;
		if ( info->cb == cb && info->param == param ) {
			info->cb = NULL;
			info->param = NULL;
		}
	}
}

/*
========================
registerBlockCallback
========================
*/
static void registerBlockCallback(	blockCallbackInfo_t * const list,
									size_t * const count,
									onMemBlockCallback cb,
									void * const param ) {
	size_t i;

	for ( i = 0; i < *count; ++i ) {
		blockCallbackInfo_t * const info = list + i;
		if ( info->cb == 0 ) {
			info->cb = cb;
			info->param = param;
			return;
		}
	}

	if ( *count != MAX_CALLBACKS ) {
		blockCallbackInfo_t * const info = list + ( *count )++;
		info->cb = cb;
		info->param = param;
	}
}

/*
========================
unregisterBlockCallback
========================
*/
static void unregisterBlockCallback(	blockCallbackInfo_t * const list,
										const size_t count,
										onMemBlockCallback cb,
										void * const param ) {
	size_t i;
	for ( i = 0; i < count; ++i ) {
		blockCallbackInfo_t * const info = list + i;
		if ( info->cb == cb && info->param == param ) {
			info->cb = NULL;
			info->param = NULL;
		}
	}
}

/*
========================
registerOnHeapCreate
========================
*/
static void registerOnHeapCreate( struct memoryInterface_type * const self, onHeapCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerHeapCallback( me->onHeapCreateCB, &me->onHeapCreateCount, cb, param );
}

/*
========================
unregisterOnHeapCreate
========================
*/
static void unregisterOnHeapCreate( struct memoryInterface_type * const self, onHeapCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterHeapCallback( me->onHeapCreateCB, me->onHeapCreateCount, cb, param );
}

/*
========================
registerOnHeapDestroy
========================
*/
static void registerOnHeapDestroy( struct memoryInterface_type * const self, onHeapCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerHeapCallback( me->onHeapDestroyCB, &me->onHeapDestroyCount, cb, param );
}

/*
========================
unregisterOnHeapDestroy
========================
*/
static void unregisterOnHeapDestroy( struct memoryInterface_type * const self, onHeapCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterHeapCallback( me->onHeapDestroyCB, me->onHeapDestroyCount, cb, param );
}

/*
========================
registerOnHeapReset
========================
*/
static void registerOnHeapReset( struct memoryInterface_type * const self, onHeapCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerHeapCallback( me->onHeapResetCB, &me->onHeapResetCount, cb, param );
}

/*
========================
unregisterOnHeapReset
========================
*/
static void unregisterOnHeapReset( struct memoryInterface_type * const self, onHeapCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterHeapCallback( me->onHeapResetCB, me->onHeapResetCount, cb, param );
}

/*
========================
registerOnMemAlloc
========================
*/
static void registerOnMemAlloc( struct memoryInterface_type * const self, onMemBlockCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerBlockCallback( me->onMemAllocCB, &me->onMemAllocCount, cb, param );
}

/*
========================
unregisterOnMemAlloc
========================
*/
static void unregisterOnMemAlloc( struct memoryInterface_type * const self, onMemBlockCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterBlockCallback( me->onMemAllocCB, me->onMemAllocCount, cb, param );
}

/*
========================
registerOnMemFree
========================
*/
static void registerOnMemFree( struct memoryInterface_type * const self, onMemBlockCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerBlockCallback( me->onMemFreeCB, &me->onMemFreeCount, cb, param );
}

/*
========================
unregisterOnMemFree
========================
*/
static void unregisterOnMemFree( struct memoryInterface_type * const self, onMemBlockCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterBlockCallback( me->onMemFreeCB, me->onMemFreeCount, cb, param );
}

/*
========================
registerOnMemTag
========================
*/
static void registerOnMemTag( struct memoryInterface_type * const self, onMemTagCallback cb, void * const param ) {
	size_t i;

	memoryData_t * const me = memoryInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	for ( i = 0; i < me->onMemTagCount; ++i ) {
		tagCallbackInfo_t * const info = me->onMemTagCB + i;
		if ( info->cb == 0 ) {
			info->cb = cb;
			info->param = param;
			return;
		}
	}

	if ( me->onMemTagCount != MAX_CALLBACKS ) {
		tagCallbackInfo_t * const info = me->onMemTagCB + me->onMemTagCount++;
		info->cb = cb;
		info->param = param;
	}
}

/*
========================
unregisterOnMemTag
========================
*/
static void unregisterOnMemTag( struct memoryInterface_type * const self, onMemTagCallback cb, void * const param ) {
	size_t i;

	memoryData_t * const me = memoryInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	for ( i = 0; i < me->onMemTagCount; ++i ) {
		tagCallbackInfo_t * const info = me->onMemTagCB + i;
		if ( info->cb == cb && info->param == param ) {
			info->cb = NULL;
			info->param = NULL;
		}
	}
}

/*
========================
registerOnMemFileLine
========================
*/
static void registerOnMemFileLine( struct memoryInterface_type * const self, onMemBlockCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerBlockCallback( me->onMemFileLineCB, &me->onMemFileLineCount, cb, param );
}

/*
========================
unregisterOnMemFileLine
========================
*/
static void unregisterOnMemFileLine( struct memoryInterface_type * const self, onMemBlockCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterBlockCallback( me->onMemFileLineCB, me->onMemFileLineCount, cb, param );
}

/*
========================
registerOnMemCallstack
========================
*/
static void registerOnMemCallstack( struct memoryInterface_type * const self, onMemBlockCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerBlockCallback( me->onMemCallstackCB, &me->onMemCallstackCount, cb, param );
}

/*
========================
unregisterOnMemCallstack
========================
*/
static void unregisterOnMemCallstack( struct memoryInterface_type * const self, onMemBlockCallback cb, void * const param ) {
	memoryData_t * const me = memoryInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterBlockCallback( me->onMemCallstackCB, me->onMemCallstackCount, cb, param );
}

/*
========================
walkHeap
========================
*/
static void walkHeap(	struct memoryInterface_type * const self,
						const uint64_t heapID,
						onMemBlockCallback cb,
						void * const param ) {
	uint32_t i;
	blockCallbackInfo_t cbInfo;

	memoryData_t * const me = memoryInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	cbInfo.cb = cb;
	cbInfo.param = param;

	for ( i = 0; i < MAX_HEAPS; ++i ) {
		heapData_t * const heap = me->heap + i;
		if ( heapID == ( uint64_t ) -1 || heap->heapID == heapID ) {
			AVLTreeWalk( heap->allocTable, AVL_WALK_LEFT_TO_RIGHT, walkCallback, &cbInfo );
		}
	}
}

/*
========================
processHeapCreateDeprecated
========================
*/
static void processHeapCreateDeprecated( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoHeapCreateDeprecated_t {
		struct packetHeader_type header;
		uint64_t heapID;
	} * const pkt = ( struct remoHeapCreateDeprecated_t* )header;

	heapData_t * const h = findHeap( me, pkt->heapID );

	h->heapStart = 0;
	h->heapSize = 0x7ffffffffff; /* maybe large enough? ok for windows... */

	processHeapNotification( me->onHeapCreateCB, me->onHeapCreateCount, h );
}

/*
========================
processHeapCreate
========================
*/
static void processHeapCreate( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoHeapCreate_t {
		struct packetHeader_type header;
		uint64_t heapID;
		uint64_t start;
		uint64_t size;
	} * const pkt = ( struct remoHeapCreate_t* )header;

	typedef struct _remoHeapCreateNamed_t {
		struct packetHeader_type header;
		uint64_t heapID;
		uint64_t start;
		uint64_t size;
		uint16_t name; /* optional */
		uint8_t padding[ 6 ];
	} remoHeapCreateNamed_t;

	heapData_t * const h = findHeap( me, pkt->heapID );

	if ( header->size == sizeof( remoHeapCreateNamed_t ) ) {
		const remoHeapCreateNamed_t * const pktName = ( const remoHeapCreateNamed_t* )header;
		h->name = me->systemInterface->findStringByID( me->systemInterface, pktName->name );
	} else {
		h->name = NULL;
	}

	h->heapStart = pkt->start;
	h->heapSize = pkt->size;

	processHeapNotification( me->onHeapCreateCB, me->onHeapCreateCount, h );
}

/*
========================
processHeapDestroy
========================
*/
static void processHeapDestroy( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoHeapDestroy_t {
		struct packetHeader_type header;
		uint64_t heapID;
	} * const pkt = ( struct remoHeapDestroy_t* )header;

	heapData_t * const h = findHeap( me, pkt->heapID );

	processHeapNotification( me->onHeapDestroyCB, me->onHeapDestroyCount, h );

	AVLTreeDestroy( h->allocTable, heapFreeCallback, me );
	memset( h, 0, sizeof( heapData_t ) );
}

/*
========================
processHeapReset
========================
*/
static void processHeapReset( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoHeapReset_t {
		struct packetHeader_type header;
		uint64_t heapID;
	} * const pkt = ( struct remoHeapReset_t* )header;

	heapData_t * const h = findHeap( me, pkt->heapID );

	processHeapNotification( me->onHeapResetCB, me->onHeapResetCount, h );

	if ( h->allocTable != 0 ) {
		AVLTreeClear( h->allocTable, heapFreeCallback, me );
	} else {
		h->allocTable = AVLTreeCreate( me->systemInterface, sizeof( uint64_t ) );
	}
}

/*
========================
processAlloc
========================
*/
static void processAlloc( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoMemAlloc_t {
		struct packetHeader_type header;
		uint64_t heapID;
		uint64_t systemAddress;
		uint64_t userAddress;
		uint32_t requestedSize;
		uint32_t actualSize;
		uint16_t tag;
		uint16_t align;
		uint32_t padding;
	} * const pkt = ( struct remoMemAlloc_t* )header;

	heapData_t * const h = findHeap( me, pkt->heapID );
	struct allocInfo_type * info;

	h->addrRangeBegin = min( h->addrRangeBegin, pkt->systemAddress );
	h->addrRangeEnd = max( h->addrRangeEnd, pkt->systemAddress + pkt->actualSize );

	info = ( struct allocInfo_type * )me->systemInterface->allocate( me->systemInterface, sizeof( struct allocInfo_type ) );
	if ( info == NULL ) {
		return;
	}

	info->time = pkt->header.time;
	info->heapID = pkt->heapID;
	info->systemAddress = pkt->systemAddress;
	info->userAddress = pkt->userAddress;
	info->callstack = 0;
	info->requestedSize = pkt->requestedSize;
	info->actualSize = pkt->actualSize;
	info->line = 0;
	info->file = 0;
	info->tag = pkt->tag;
	info->align = pkt->align;
	info->callstackDepth = 0;

	if ( pkt->header.size > sizeof( struct remoMemAlloc_t ) ) {
		const uint64_t * const callstack = ( const uint64_t* )( pkt + 1 );
		const size_t count = ( ( size_t )pkt->header.size - sizeof( struct remoMemAlloc_t ) ) / sizeof( uint64_t );

		info->callstack = me->systemInterface->allocate( me->systemInterface, sizeof( uint64_t ) * count );

		if ( info->callstack ) {
			memcpy( info->callstack, callstack, sizeof( uint64_t ) * count );
			info->callstackDepth = ( uint8_t )count;
		} else {
			info->callstackDepth = 0;
		}
	}

	if ( !AVLTreeInsert( h->allocTable, &pkt->userAddress, info ) ) {
#if 1 /* debugging */
		int i = 0;
		( void )i;
#endif /* 0 */
	}

	h->requestedBytes += pkt->requestedSize;
	h->actualBytes += pkt->actualSize;

	processBlockNotification( me->onMemAllocCB, me->onMemAllocCount, info );
}

/*
========================
processFree
========================
*/
static void processFree( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoMemFree_t {
		struct packetHeader_type header;
		uint64_t userAddress;
	} * const pkt = ( struct remoMemFree_t* )header;

	heapData_t *h = NULL;
	struct allocInfo_type * info = NULL;
	size_t i;

	for ( i = 0; i < me->heapCount; ++i ) {
		h = me->heap + i;
		if ( AVLTreeRemove( h->allocTable, &pkt->userAddress, ( void** )&info ) ) {
			break;
		}
	}

	if ( info == NULL ) {
		return;
	}

	processBlockNotification( me->onMemFreeCB, me->onMemFreeCount, info );

	h->requestedBytes -= info->requestedSize;
	h->actualBytes -= info->actualSize;

	me->systemInterface->deallocate( me->systemInterface, info->callstack );
	me->systemInterface->deallocate( me->systemInterface, info );
}

/*
========================
processTag
========================
*/
static void processTag( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoMemTag_t {
		struct packetHeader_type header;
		uint16_t tag;
		char name[ 16 ];
		uint8_t padding[ 6 ];
	} * const pkt = ( struct remoMemTag_t* )header;

	size_t i;

	if ( pkt->tag < MAX_TAGS ) {
		char tmp[ sizeof( pkt->name ) + 1 ];
		strncpy( tmp, pkt->name, sizeof( pkt->name ) );
		tmp[ sizeof( pkt->name ) ] = 0;
		me->tags[ pkt->tag ].name = me->systemInterface->addString( me->systemInterface, tmp );
	}

	for ( i = 0; i < me->onMemTagCount; ++i ) {
		tagCallbackInfo_t * const info = me->onMemTagCB + i;
		if ( info->cb != NULL ) {
			info->cb( info->param, pkt->tag, pkt->name );
		}
	}
}

/*
========================
processFileLine
========================
*/
static void processFileLine( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoMemFileLine_t {
		struct packetHeader_type header;
		uint64_t userAddress;
		uint32_t line;
		uint16_t file;
		uint16_t padding;
	} * const pkt = ( struct remoMemFileLine_t* )header;

	struct allocInfo_type * info = NULL;
	size_t i;

	for ( i = 0; i < me->heapCount; ++i ) {
		heapData_t * const h = me->heap + i;
		if ( AVLTreeFind( h->allocTable, &pkt->userAddress, ( void** )&info ) ) {
			break;
		}
	}

	if ( info == NULL ) {
		return;
	}

	info->file = pkt->file;
	info->line = pkt->line;

	processBlockNotification( me->onMemFileLineCB, me->onMemFileLineCount, info );
}

/*
========================
processCallstack
========================
*/
static void processCallstack( memoryData_t * const me, const struct packetHeader_type * const header ) {
	struct remoMemCallstack_t {
		struct packetHeader_type header;
		uint64_t userAddress;
		uint8_t count;
		uint8_t padding[ 7 ];
		uint64_t list[ 1 ]; /* placeholder for real array */
	} * const pkt = ( struct remoMemCallstack_t* )header;

	struct allocInfo_type * info = 0;
	size_t i;

	for ( i = 0; i < me->heapCount; ++i ) {
		heapData_t * const h = me->heap + i;
		if ( AVLTreeFind( h->allocTable, &pkt->userAddress, ( void** )&info ) ) {
			break;
		}
	}

	if ( info == NULL ) {
		return;
	}

	me->systemInterface->deallocate( me->systemInterface, info->callstack );

	info->callstack = me->systemInterface->allocate( me->systemInterface, sizeof( uint64_t ) * pkt->count );

	if ( info->callstack ) {
		memcpy( info->callstack, pkt->list, sizeof( uint64_t ) * pkt->count );
		info->callstackDepth = pkt->count;
	} else {
		info->callstackDepth = 0;
	}

	processBlockNotification( me->onMemCallstackCB, me->onMemCallstackCount, info );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	memoryData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_HEAP_CREATE,				processHeapCreate,				me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_HEAP_DESTROY,			processHeapDestroy,				me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_HEAP_RESET,				processHeapReset,				me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_ALLOC,				processAlloc,					me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_FREE,				processFree,					me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_TAG,					processTag,						me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_HEAP_CREATE_DEPRECATED1,	processHeapCreateDeprecated,	me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_FILELINE,			processFileLine,				me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_CALLSTACK,			processCallstack,				me );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	memoryData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_CALLSTACK,				processCallstack,				me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_FILELINE,				processFileLine,				me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_HEAP_CREATE_DEPRECATED1,	processHeapCreateDeprecated,	me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_TAG,					processTag,						me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_FREE,					processFree,					me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_MEM_ALLOC,					processAlloc,					me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_HEAP_RESET,				processHeapReset,				me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_HEAP_DESTROY,				processHeapDestroy,				me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_MEMORY, PACKET_HEAP_CREATE,				processHeapCreate,				me );
}

/*
========================
myReport
========================
*/
typedef struct _reportTagStat_t {
	uint64_t requestedSize;
	uint64_t actualSize;
} reportTagStat_t;

/*
========================
myReportMemWalkCB
========================
*/
static void myReportMemWalkCB( void * const param, const void * const key, const void * const data ) {
	reportTagStat_t * const arg = ( reportTagStat_t* )param;
	struct allocInfo_type * const info = ( struct allocInfo_type * )data;

	( void )key;

	arg[ info->tag ].requestedSize += info->requestedSize;
	arg[ info->tag ].actualSize += info->actualSize;
}

/*
========================
gilobite
========================
*/
static void gilobite( const uint64_t value, double * const result, const char ** const ext ) {
	double tmp = ( double )value;

	if ( tmp > 1024.0 ) {
		tmp /= 1024.0;
		*ext = "K";
		if ( tmp > 1024.0 ) {
			tmp /= 1024.0;
			*ext = "M";
			if ( tmp > 1024.0 ) {
				tmp /= 1024.0;
				*ext = "G";
			}
		}
	}

	*result = tmp;
}

/*
========================
myReport
========================
*/
static void myReport( struct pluginInterface_type * const self, FILE * const output ) {
	uint32_t i;
	uint32_t n;

	memoryData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	for ( i = 0; i < MAX_HEAPS; ++i ) {
		heapData_t * const h = me->heap + i;
		if ( h->allocTable != 0 ) {
			reportTagStat_t arg[ MAX_TAGS ];
			uint64_t totalRequested = 0;
			uint64_t totalActual = 0;
			double tmp;
			const char *ext = "";

			memset( arg, 0, sizeof( arg ) );

			AVLTreeWalk( h->allocTable, AVL_WALK_LEFT_TO_RIGHT, myReportMemWalkCB, arg );
			for ( n = 0; n < MAX_TAGS; ++n ) {
				totalRequested += arg[ n ].requestedSize;
				totalActual += arg[ n ].actualSize;
			}

			fprintf( output, "Heap: %" PRIx64 " ", h->heapID );

			gilobite( totalRequested, &tmp, &ext );
			fprintf( output, "Requested: %.2f%sB ", tmp, ext );

			gilobite( totalActual, &tmp, &ext );
			fprintf( output, "Actual: %.2f%sB ", tmp, ext );

			fprintf( output, "\nTag\tRequested\tActual\n" );
			for ( n = 0; n < MAX_TAGS; ++n ) {
				if ( me->tags[ n ].name[ 0 ] ) {
					fprintf(	output,
								"%s\t%" PRIu64 "\t%" PRIu64 "\n",
								me->tags[ n ].name,
								arg[ n ].requestedSize,
								arg[ n ].actualSize );
				}
			}
			fprintf( output, "\n" );
		}
	}
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_MEMORY;
}

/*
========================
myGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	memoryData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return &me->memoryInterface;
}

/*
========================
Memory_Create
========================
*/
struct pluginInterface_type * Memory_Create( struct systemInterface_type * const sys ) {
	memoryData_t * const me = sys->allocate( sys, sizeof( memoryData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( memoryData_t ) );

	me->checksum = PLUGIN_MEMORY_CHECKSUM;

	me->pluginInterface.start						= myStart;
	me->pluginInterface.stop						= myStop;
	me->pluginInterface.report						= myReport;
	me->pluginInterface.getName						= myGetName;
	me->pluginInterface.getPrivateInterface			= myGetPrivateInterface;

	me->memoryInterface.registerOnHeapCreate		= registerOnHeapCreate;
	me->memoryInterface.unregisterOnHeapCreate		= unregisterOnHeapCreate;
	me->memoryInterface.registerOnHeapDestroy		= registerOnHeapDestroy;
	me->memoryInterface.unregisterOnHeapDestroy		= unregisterOnHeapDestroy;
	me->memoryInterface.registerOnHeapReset			= registerOnHeapReset;
	me->memoryInterface.unregisterOnHeapReset		= unregisterOnHeapReset;
	me->memoryInterface.registerOnMemAlloc			= registerOnMemAlloc;
	me->memoryInterface.unregisterOnMemAlloc		= unregisterOnMemAlloc;
	me->memoryInterface.registerOnMemFree			= registerOnMemFree;
	me->memoryInterface.unregisterOnMemFree			= unregisterOnMemFree;
	me->memoryInterface.registerOnMemTag			= registerOnMemTag;
	me->memoryInterface.unregisterOnMemTag			= unregisterOnMemTag;
	me->memoryInterface.registerOnMemFileLine		= registerOnMemFileLine;
	me->memoryInterface.unregisterOnMemFileLine		= unregisterOnMemFileLine;
	me->memoryInterface.registerOnMemCallstack		= registerOnMemCallstack;
	me->memoryInterface.unregisterOnMemCallstack	= unregisterOnMemCallstack;
	me->memoryInterface.walkHeap					= walkHeap;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
