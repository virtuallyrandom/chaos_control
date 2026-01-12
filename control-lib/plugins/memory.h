/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __MEMORY_H__
#define __MEMORY_H__

struct systemInterface_type;
struct pluginInterface_type;

struct allocInfo_type {
	uint64_t	time;
	uint64_t	heapID;
	uint64_t	systemAddress;
	uint64_t	userAddress;
	uint64_t*	callstack;
	uint32_t	requestedSize;
	uint32_t	actualSize;
	uint32_t	line;
	uint16_t	file;
	uint16_t	tag;
	uint16_t	align;
	uint8_t		callstackDepth;
	uint8_t		padding[5];
};

struct heapInfo_type {
	const char*	name;
	uint64_t	heapID;
	uint64_t	addrRangeBegin;
	uint64_t	addrRangeEnd;
	uint64_t	heapStart;
	uint64_t	heapSize;
	uint32_t	requestedBytes;
	uint32_t	actualBytes;
};

typedef void ( * onHeapCallback )( void * const param, const struct heapInfo_type * const );
typedef void ( * onMemBlockCallback )( void * const param, const struct allocInfo_type * const );
typedef void ( * onMemTagCallback )( void * const param, const uint16_t tag, const char * const name );

struct memoryInterface_type {
	void ( * registerOnHeapCreate		)( struct memoryInterface_type * const, onHeapCallback, void * const param );
	void ( * unregisterOnHeapCreate		)( struct memoryInterface_type * const, onHeapCallback, void * const param );
	void ( * registerOnHeapDestroy		)( struct memoryInterface_type * const, onHeapCallback, void * const param );
	void ( * unregisterOnHeapDestroy	)( struct memoryInterface_type * const, onHeapCallback, void * const param );
	void ( * registerOnHeapReset		)( struct memoryInterface_type * const, onHeapCallback, void * const param );
	void ( * unregisterOnHeapReset		)( struct memoryInterface_type * const, onHeapCallback, void * const param );
	void ( * registerOnMemAlloc			)( struct memoryInterface_type * const, onMemBlockCallback, void * const param );
	void ( * unregisterOnMemAlloc		)( struct memoryInterface_type * const, onMemBlockCallback, void * const param );
	void ( * registerOnMemFree			)( struct memoryInterface_type * const, onMemBlockCallback, void * const param );
	void ( * unregisterOnMemFree		)( struct memoryInterface_type * const, onMemBlockCallback, void * const param );
	void ( * registerOnMemTag			)( struct memoryInterface_type * const, onMemTagCallback, void * const param );
	void ( * unregisterOnMemTag			)( struct memoryInterface_type * const, onMemTagCallback, void * const param );
	void ( * registerOnMemFileLine		)( struct memoryInterface_type * const, onMemBlockCallback, void * const param );
	void ( * unregisterOnMemFileLine	)( struct memoryInterface_type * const, onMemBlockCallback, void * const param );
	void ( * registerOnMemCallstack		)( struct memoryInterface_type * const, onMemBlockCallback, void * const param );
	void ( * unregisterOnMemCallstack	)( struct memoryInterface_type * const, onMemBlockCallback, void * const param );
	void ( * walkHeap					)( struct memoryInterface_type * const, const uint64_t heapID, onMemBlockCallback, void * const param );
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_MEMORY[];
struct pluginInterface_type * Memory_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __MEMORY_H__ */
