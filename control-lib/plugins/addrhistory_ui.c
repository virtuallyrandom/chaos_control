/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "memory.h"
#include "plugin.h"
#include "symbol.h"
#include "../ctl_tree.h"
#include "../ring.h"
#include "../list.h"

/*
	TODO:

	This plugin needs to use:
		- replay
		- memory
		- symbol

	Initialize/Shutdown changes to Create/Destroy with a new init/shutdown that's empty
	The global 'me' pointer needs to be a per-instantiation context handle.
	The windowing/ui needs to be abstracted.
*/

#define PLUGIN_ADDRHISTORYUI_CHECKSUM 0x4144445248495354ULL /* 'ADDRHIST' */

enum column_t {
	COL_STATE,
	COL_TIME,
	COL_HEAP,
	COL_SYSSTART,
	COL_SYSEND,
	COL_SYSSIZE,
	COL_USERSTART,
	COL_USEREND,
	COL_USERSIZE,
	COL_FILE,
	COL_CALLSTACK,
};

#define FONT_HEIGHT 14
#define MAX_EDIT_NUMERIC_LENGTH 20
#define MAX_EDIT_PATH_LENGTH 256

typedef struct _extendedAllocInfo_t {
	struct allocInfo_type	allocInfo;
	int32_t					state;
	int32_t					index; /* Only valid when using findAlloc and only to the end of the function! */
} extendedAllocInfo_t;

typedef struct _columnInfo_t {
	const char*	name;
	int32_t		columnFormat;
	int32_t		index;
	int32_t		sort;
	int32_t		padding;
} columnInfo_t;

typedef struct _searchInfo_t {
	struct _addrHistoryUIData_t* me;
	uintptr_t addrMin;
	uintptr_t addrMax;
	HANDLE thread;
	DWORD threadID;
	DWORD padding;
	char path[ MAX_EDIT_PATH_LENGTH ];
} searchInfo_t;

static columnInfo_t reportColumn[] = {
	{ "State",			LVCFMT_RIGHT,	COL_STATE,		0 },
	{ "Time",			LVCFMT_RIGHT,	COL_TIME,		0 },
	{ "Heap",			LVCFMT_RIGHT,	COL_HEAP,		0 },
	{ "Actual Start",	LVCFMT_RIGHT,	COL_SYSSTART,	0 },
	{ "Actual End",		LVCFMT_RIGHT,	COL_SYSEND,		0 },
	{ "Actual Size",	LVCFMT_RIGHT,	COL_SYSSIZE,	0 },
	{ "User Start",		LVCFMT_RIGHT,	COL_USERSTART,	0 },
	{ "User End",		LVCFMT_RIGHT,	COL_USEREND,	0 },
	{ "User Size",		LVCFMT_RIGHT,	COL_USERSIZE,	0 },
	{ "File",			LVCFMT_LEFT,	COL_FILE,		0 },
	{ "Callstack",		LVCFMT_LEFT,	COL_CALLSTACK,	0 },
};
static const int32_t reportColumnCount = sizeof( reportColumn ) / sizeof( *reportColumn );

typedef struct _addrHistoryUIData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct systemInterface_type *	systemInterface;
	symbolInterface_t*				symbolInterface;
	HWND							wnd;
	HWND							report;
	HWND							labelRangeMin;
	HWND							editRangeMin;
	HWND							labelRangeMax;
	HWND							editRangeMax;
	HWND							labelBinPath;
	HWND							editBinPath;
	HWND							buttonGo;
	HWND							buttonCancel;
	HWND							progress;
	LONG_PTR						prevProc;
	HFONT							font;
	int32_t							cancelSearch;
	uint32_t						sortOnColumn;
	list_t							list;
	const char *					name;
	const char *					defaultBinPath;
} addrHistoryUIData_t;

typedef struct _addrHistoryUIData_t* addrHistoryUI_t;

/*
========================
pluginInterfaceToMe
========================
*/
static addrHistoryUI_t pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_ADDRHISTORYUI_CHECKSUM ) {
			return ( addrHistoryUI_t )checksum;
		}
	}
	return NULL;
}

/*
========================
copyCallstack
========================
*/
static void copyCallstack(	addrHistoryUI_t const me,
							const uint64_t * const list,
							const uint8_t count,
							extendedAllocInfo_t * const eai ) {
	if ( eai->allocInfo.callstack ) {
		me->systemInterface->deallocate( me->systemInterface, eai->allocInfo.callstack );
	}

	if ( count == 0 ) {
		eai->allocInfo.callstack = NULL;
		eai->allocInfo.callstackDepth = 0;
		return;
	}

	eai->allocInfo.callstackDepth = count;
	eai->allocInfo.callstack = me->systemInterface->allocate( me->systemInterface, sizeof( uint64_t ) * count );

	memcpy( eai->allocInfo.callstack, list, sizeof( uint64_t ) * count );
}

/*
========================
printCallstack
========================
*/
static size_t printCallstack(	addrHistoryUI_t const me,
								const uint64_t * const list,
								const uint8_t count,
								char * const text,
								const size_t length ) {
	size_t txti = 0;
	size_t i;
	for ( i = 0; i < count; ++i ) {
		const char *name;

		if ( 0 == me->symbolInterface->find( me->symbolInterface, list[ i ], &name, NULL, NULL ) ) {
			txti += snprintf( text + txti, length - txti, "0x%" PRIx64, list[ i ] );
		} else {
			txti += snprintf( text + txti, length - txti, "%s", name );
		}

		if ( i < count - 1u ) {
			txti += snprintf( text + txti, length - txti, " <- " );
		}
	}

	text[ length - 1 ] = 0;

	return txti;
}

/*
========================
findAlloc
========================
*/
static void findAlloc(	addrHistoryUI_t const me,
						const uint64_t userAddr,
						extendedAllocInfo_t ** const alloc,
						extendedAllocInfo_t ** const free ) {
	extendedAllocInfo_t **first = NULL;
	extendedAllocInfo_t **last = NULL;

	if ( alloc ) {
		*alloc = NULL;
	}

	if ( free ) {
		*free = NULL;
	}

	List_Range( me->list, ( void** )&first, ( void** )&last );

	while ( first != last ) {
		extendedAllocInfo_t * const eai = *first;

		if ( eai->allocInfo.userAddress == userAddr ) {
			LRESULT index;
			LVFINDINFO find;
			find.flags = LVFI_PARAM;
			find.lParam = ( LPARAM )eai;

			index = SendMessage( me->report, LVM_FINDITEMA, 0, ( LPARAM )&find );

			if ( alloc && eai->state == 1 && *alloc == NULL ) {
				*alloc = eai;
				eai->index = ( int32_t )index;
			}
				
			if ( free && eai->state == 0 && *free == NULL ) {
				*free = eai;
				eai->index = ( int32_t )index;
			}

			if ( ( !alloc || *alloc ) && ( !free || *free ) ) {
				return;
			}
		}

		first++;
	}
}

/*
========================
updateAlloc
========================
*/
static void updateAlloc( addrHistoryUI_t const me, extendedAllocInfo_t * const eai ) {
	LVITEM itm;
	char txt[ 4096 ];

	memset( &itm, 0, sizeof( itm ) );

	if ( eai->index == -1 ) {
		LRESULT last = SendMessage( me->report, LVM_GETITEMCOUNT, 0, 0 ) - 1;

		/* find the insertion point that keeps everything sorted */
		while ( last > 0 ) {
			itm.mask = LVIF_PARAM;
			if ( SendMessageA( me->report, LVM_GETITEMA, 0, ( LPARAM )&itm ) ) {
				const extendedAllocInfo_t * const test = ( const extendedAllocInfo_t* )itm.lParam;
				if ( eai->allocInfo.time >= test->allocInfo.time ) {
					break;
				}
			}
			last--;
		}

		itm.mask = LVIF_TEXT | LVIF_PARAM;

		itm.pszText = eai->state ? "Alloc" : "Free";
		itm.lParam = ( LPARAM )eai;
		itm.iItem = ( int32_t )( last + 1 );
		itm.iItem = ( int )SendMessageA( me->report, LVM_INSERTITEMA, 0, ( LPARAM )&itm );

	} else {
		itm.iItem = eai->index;
	}

	itm.mask = LVIF_TEXT;
	itm.pszText = txt;

	/* time */
	itm.iSubItem = COL_TIME;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%" PRIx64, eai->allocInfo.time );
	txt[ sizeof( txt ) - 1 ] = 0;
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* heap */
	itm.iSubItem = COL_HEAP;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%" PRIx64, eai->allocInfo.heapID );
	txt[ sizeof( txt ) - 1 ] = 0;
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* system start */
	itm.iSubItem = COL_SYSSTART;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%" PRIx64, eai->allocInfo.systemAddress );
	txt[ sizeof( txt ) - 1 ] = 0;
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* system end */
	itm.iSubItem = COL_SYSEND;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%" PRIx64, eai->allocInfo.systemAddress + eai->allocInfo.actualSize );
	txt[ sizeof( txt ) - 1 ] = 0;
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* actual size */
	itm.iSubItem = COL_SYSSIZE;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%u", eai->allocInfo.actualSize );
	txt[ sizeof( txt ) - 1 ] = 0;
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* user start */
	itm.iSubItem = COL_USERSTART;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%" PRIx64, eai->allocInfo.userAddress );
	txt[ sizeof( txt ) - 1 ] = 0;
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* user end */
	itm.iSubItem = COL_USEREND;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%" PRIx64, eai->allocInfo.userAddress + eai->allocInfo.requestedSize );
	txt[ sizeof( txt ) - 1 ] = 0;
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* requested size */
	itm.iSubItem = COL_USERSIZE;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%u", eai->allocInfo.requestedSize );
	txt[ sizeof( txt ) - 1 ] = 0;
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	if ( eai->allocInfo.file != ( uint16_t )-1 ) {
		itm.iSubItem = COL_FILE;
		_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%s", me->systemInterface->findStringByID( me->systemInterface, eai->allocInfo.file ) );
		txt[ sizeof( txt ) - 1 ] = 0;
		SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );
	}

	if ( eai->allocInfo.callstack ) {
		printCallstack( me, eai->allocInfo.callstack, eai->allocInfo.callstackDepth, txt, sizeof( txt ) );
		itm.iSubItem = COL_CALLSTACK;
		SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );
	}
}

/*
========================
reportAlloc
========================
*/
static void reportAlloc(	addrHistoryUI_t const me,
							const uintptr_t addrMin,
							const uintptr_t addrMax,
							const struct packetHeader_type * const hdr ) {
	typedef struct _remoMemAlloc_t {
		struct packetHeader_type header;
		uint64_t heapID;
		uint64_t systemAddress;
		uint64_t userAddress;
		uint32_t requestedSize;
		uint32_t actualSize;
		uint16_t tag;
		uint16_t align;
		uint32_t padding;
	} remoMemAlloc_t;

	const remoMemAlloc_t * const pkt = ( const remoMemAlloc_t* )hdr;
	extendedAllocInfo_t *eai;

	if ( pkt->systemAddress > addrMax ) {
		return;
	}

	if ( pkt->systemAddress + pkt->actualSize < addrMin ) {
		return;
	}

	eai = ( extendedAllocInfo_t* )me->systemInterface->allocate( me->systemInterface, sizeof( extendedAllocInfo_t ) );
	if ( eai == 0 ) {
		return;
	}

	memset( eai, 0, sizeof( extendedAllocInfo_t ) );

	eai->state = 1;
	eai->index = -1;
	eai->allocInfo.time = hdr->time;
	eai->allocInfo.heapID = pkt->heapID;
	eai->allocInfo.systemAddress = pkt->systemAddress;
	eai->allocInfo.userAddress = pkt->userAddress;
	eai->allocInfo.callstack = NULL;
	eai->allocInfo.callstackDepth = 0;
	eai->allocInfo.requestedSize = pkt->requestedSize;
	eai->allocInfo.actualSize = pkt->actualSize;
	eai->allocInfo.line = 0;
	eai->allocInfo.file = ( uint16_t )-1;
	eai->allocInfo.tag = pkt->tag;
	eai->allocInfo.align = pkt->align;

	if ( hdr->size > sizeof( remoMemAlloc_t ) ) {
		const uint64_t * const callstack = ( const uint64_t* )( pkt + 1 );
		const uint8_t count = ( uint8_t )( hdr->size - sizeof( remoMemAlloc_t ) ) / sizeof( uint64_t );
		copyCallstack( me, callstack, count, eai );
	}

	updateAlloc( me, eai );

	List_Append( me->list, &eai );
}

/*
========================
reportFileLine
========================
*/
static void reportFileLine(	addrHistoryUI_t const me,
							const uintptr_t addrMax,
							const struct packetHeader_type * const hdr ) {
	typedef struct _remoMemFileLine_t {
		struct packetHeader_type header;
		uint64_t userAddress;
		uint32_t line;
		uint16_t file;
		uint16_t padding;
	} remoMemFileLine_t;

	const remoMemFileLine_t * const pkt = ( const remoMemFileLine_t* )hdr;

	LVITEM itm;
	char txt[ 512 ];
	extendedAllocInfo_t* lastAlloc = NULL;
	extendedAllocInfo_t* lastFree = NULL;

	if ( pkt->userAddress > addrMax ) {
		return;
	}

	findAlloc( me, pkt->userAddress, &lastAlloc, &lastFree );

	if ( lastAlloc ) {
		lastAlloc->allocInfo.file = pkt->file;
		memset( &itm, 0, sizeof( itm ) );

		itm.iItem = lastAlloc->index;
		itm.iSubItem = COL_FILE;
		_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%s", me->systemInterface->findStringByID( me->systemInterface, pkt->file ) );
		txt[ sizeof( txt ) - 1 ] = 0;
		SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );
	}

	if ( lastFree ) {
		lastFree->allocInfo.file = pkt->file;
		memset( &itm, 0, sizeof( itm ) );

		itm.iItem = lastFree->index;
		itm.iSubItem = COL_FILE;
		_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%s", me->systemInterface->findStringByID( me->systemInterface, pkt->file ) );
		txt[ sizeof( txt ) - 1 ] = 0;
		SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );
	}
}

/*
========================
reportCallstack
========================
*/
static void reportCallstack(	addrHistoryUI_t const me,
								const uintptr_t addrMax,
								const struct packetHeader_type * const hdr ) {
	typedef struct _remoMemCallstack_t {
		struct packetHeader_type header;
		uint64_t userAddress;
		uint8_t count;
		uint8_t padding[ 7 ];
		uint64_t list[ 1 ]; /* placeholder for real array */
	} remoMemCallstack_t;

	const remoMemCallstack_t * const pkt = ( const remoMemCallstack_t* )hdr;

	LVITEM itm;
	char txt[ 4096 ];
	extendedAllocInfo_t* lastAlloc;
	extendedAllocInfo_t* lastFree;

	if ( pkt->userAddress > addrMax ) {
		return;
	}

	findAlloc( me, pkt->userAddress, &lastAlloc, &lastFree );

	if ( lastAlloc ) {
		copyCallstack( me, pkt->list, pkt->count, lastAlloc );

		printCallstack( me, pkt->list, pkt->count, txt, sizeof( txt ) );

		memset( &itm, 0, sizeof( itm ) );
		itm.iItem = lastAlloc->index;
		itm.iSubItem = COL_CALLSTACK;
		SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );
	}

	if ( lastFree ) {
		copyCallstack( me, pkt->list, pkt->count, lastFree );

		printCallstack( me, pkt->list, pkt->count, txt, sizeof( txt ) );

		memset( &itm, 0, sizeof( itm ) );
		itm.iItem = lastFree->index;
		itm.iSubItem = COL_CALLSTACK;
		SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );
	}
}

/*
========================
reportFree
========================
*/
static void reportFree(	addrHistoryUI_t const me,
						const uintptr_t addrMax,
						const struct packetHeader_type * const hdr ) {
	typedef struct _remoMemFree_t {
		struct packetHeader_type header;
		uint64_t userAddress;
	} remoMemFree_t;

	const remoMemFree_t * const pkt = ( const remoMemFree_t* )hdr;

	extendedAllocInfo_t* lastAlloc;

	if ( pkt->userAddress > addrMax ) {
		return;
	}

	findAlloc( me, pkt->userAddress, &lastAlloc, NULL );

	if ( lastAlloc ) {
		extendedAllocInfo_t * const eai = me->systemInterface->allocate( me->systemInterface, sizeof( extendedAllocInfo_t ) );

		if ( eai == 0 ) {
			return;
		}

		memcpy( eai, lastAlloc, sizeof( extendedAllocInfo_t ) );

		eai->state = 0;
		eai->index = -1;
		eai->allocInfo.callstack = 0;
		eai->allocInfo.callstackDepth = 0;

		copyCallstack( me, lastAlloc->allocInfo.callstack, lastAlloc->allocInfo.callstackDepth, eai );

		updateAlloc( me, eai );
	}
}

/*
========================
clearReport
========================
*/
static void clearReport( addrHistoryUI_t const me ) {
	LRESULT count;
	LVITEM itm;

	memset( &itm, 0, sizeof( itm ) );
	count = SendMessageA( me->report, LVM_GETITEMCOUNT, 0, 0 );

	while ( count >= 0 ) {
		itm.mask = LVIF_PARAM;
		itm.iItem = ( int )count--;
		if ( SendMessageA( me->report, LVM_GETITEMA, 0, ( LPARAM )&itm ) ) {
			extendedAllocInfo_t * const eai = ( extendedAllocInfo_t* )itm.lParam;
			if ( eai->allocInfo.callstack ) {
				me->systemInterface->deallocate( me->systemInterface, eai->allocInfo.callstack );
			}
			me->systemInterface->deallocate( me->systemInterface, eai );
		}
	}

	SendMessageA( me->report, LVM_DELETEALLITEMS, 0, 0 );
}

/*
========================
binSearch
========================
*/
static DWORD WINAPI binSearch( searchInfo_t * const searchInfo ) {
	#define SYSTEM_MEMORY			1
	#define PACKET_MEM_ALLOC		4
	#define PACKET_MEM_FREE			5
	#define PACKET_MEM_FILELINE		7
	#define PACKET_MEM_CALLSTACK	9

	static const int32_t RING_SIZE = 32 * 1024 * 1024;

	typedef struct _tableEntry_t {
		extendedAllocInfo_t *list;
		size_t count;
	} tableEntry_t;

	struct ringBuffer_type * ring;

	addrHistoryUI_t const me = searchInfo->me;

	me->cancelSearch = 0;

	SendMessage( me->progress, PBM_SETPOS, 0, 0 );

	ShowWindow( me->labelRangeMin, SW_HIDE );
	ShowWindow( me->editRangeMin, SW_HIDE );
	ShowWindow( me->labelRangeMax, SW_HIDE );
	ShowWindow( me->editRangeMax, SW_HIDE );
	ShowWindow( me->labelBinPath, SW_HIDE );
	ShowWindow( me->editBinPath, SW_HIDE );
	ShowWindow( me->buttonGo, SW_HIDE );

	ShowWindow( me->buttonCancel, SW_SHOW );
	ShowWindow( me->progress, SW_SHOW );
	EnableWindow( me->buttonCancel, TRUE );

	EnableWindow( me->labelRangeMin, FALSE );
	EnableWindow( me->editRangeMin, FALSE );
	EnableWindow( me->labelRangeMax, FALSE );
	EnableWindow( me->editRangeMax, FALSE );
	EnableWindow( me->labelBinPath, FALSE );
	EnableWindow( me->editBinPath, FALSE );
	EnableWindow( me->buttonGo, FALSE );

	clearReport( me );

	ring = RingCreate( RING_SIZE );
	if ( ring ) {
		HANDLE h;
		OVERLAPPED overlapped;
		DWORD read;

		memset( &overlapped, 0, sizeof( overlapped ) );
		overlapped.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );

		h = CreateFileA(	searchInfo->path,
							GENERIC_READ,
							FILE_SHARE_READ | FILE_SHARE_WRITE,
							0,
							OPEN_EXISTING,
							FILE_ATTRIBUTE_NORMAL,
							0 );

		if ( h != INVALID_HANDLE_VALUE ) {
			typedef struct _remoHeader_t {
				char fourcc[ 4 ];
				uint32_t version;
				uint32_t endian;
			} remoHeader_t;

			remoHeader_t fileHeader;
			int ok;

			ok = ReadFile( h, &fileHeader, sizeof( fileHeader ), &read, 0 );
			ok = ok && read == sizeof( fileHeader );
			ok = ok && memcmp( fileHeader.fourcc, "REMO", 4 ) == 0;
			ok = ok && fileHeader.version >= 0x00020000;
			if ( ok ) {
				uint8_t *pos;
				size_t size;
				size_t writeAvail = RING_SIZE / 2;

				LARGE_INTEGER remainingBytes;

				GetFileSizeEx( h, &remainingBytes );
				remainingBytes.QuadPart -= sizeof( fileHeader );

				size = min( remainingBytes.QuadPart, RING_SIZE / 2 );

				if ( RingWriteLock( ring, size, &pos, &size ) ) {
					int64_t processedBytes = sizeof( remoHeader_t );
					int keepProcessing = 1;
					int32_t lastProgressPosition = 0;
					uint8_t *ptr = 0;

					overlapped.Offset = ( uint32_t )processedBytes;
					overlapped.OffsetHigh = ( uint32_t )( processedBytes >> 32 );
					( void )ReadFile( h, pos, ( DWORD )size, &read, &overlapped );

					while ( !me->cancelSearch && keepProcessing ) {
						LARGE_INTEGER li;
						int64_t numerator;
						int64_t divisor = 1;
						size_t readAvail;
						size_t readProcessed = 0;

						if ( !GetFileSizeEx( h, &li ) ) {
							break;
						}

						numerator = li.QuadPart;
						while ( numerator & 0xffffffffff000000 ) {
							divisor <<= 1;
							numerator >>= 1;
						}

						/* wait for the last read to complete */
						WaitForSingleObject( overlapped.hEvent, INFINITE );
						RingWriteUnlock( ring, writeAvail );

						processedBytes += writeAvail;

						/* how much can we write in a single shot to the ring buffer */
						writeAvail = RingWriteAvail( ring );
						writeAvail = min( writeAvail, RING_SIZE / 2u );
						writeAvail = min( writeAvail, ( size_t )remainingBytes.QuadPart );
						
						/* this should never fail as we're always writing half of the data */
						if ( !RingWriteLock( ring, writeAvail, &pos, &size ) ) {
							/* well, this is bad... */
							break;
						}

						/* latently read the next block of data */
						overlapped.Offset = ( uint32_t )processedBytes;
						overlapped.OffsetHigh = ( uint32_t )( processedBytes >> 32 );
						if ( !ReadFile( h, pos, ( DWORD )size, &read, &overlapped ) && ERROR_IO_PENDING != GetLastError() ) {
							keepProcessing = 0;
						}

						/* read a block of data */
						readAvail = RingReadAvail( ring );
						if ( !RingReadLock( ring, readAvail, &pos, &size ) ) {
							break;
						}

						if ( !ptr ) {
							ptr = pos;
						}

						ptr = RingWrap( ring, ptr );

						/* process this block */
						for ( ;; ) {
							const struct packetHeader_type * const pkt = ( const struct packetHeader_type * )ptr;

							if ( me->cancelSearch ) {
								break;
							}

							if ( pkt->size == 0 || pkt->size > 1024 || pkt->systemID > 7 || pkt->packetID > 11 ) {
								ptr = RingWrap( ring, ptr + 1 );
								continue;
							}

							if ( readProcessed + pkt->size > readAvail ) {
								break;
							}

							if ( pkt->systemID == SYSTEM_MEMORY ) {
								switch ( pkt->packetID ) {
									case PACKET_MEM_ALLOC:
										reportAlloc( me, searchInfo->addrMin, searchInfo->addrMax, pkt );
										break;

									case PACKET_MEM_FREE:
										reportFree( me, searchInfo->addrMax, pkt );
										break;

									case PACKET_MEM_FILELINE:
										reportFileLine( me, searchInfo->addrMax, pkt );
										break;

									case PACKET_MEM_CALLSTACK:
										reportCallstack( me, searchInfo->addrMax, pkt );
										break;
								}
							}

							readProcessed += ( size_t )pkt->size;
							ptr += ( size_t )pkt->size;
						}

						RingReadUnlock( ring, readProcessed );

						if ( ( ( processedBytes / divisor ) * 100 ) / ( li.QuadPart / divisor ) != lastProgressPosition ) {
							lastProgressPosition = ( int32_t )( ( ( processedBytes / divisor ) * 100 ) / ( li.QuadPart / divisor ) );
							SendMessage( me->progress, PBM_SETPOS, lastProgressPosition, 0 );
						}
					}
				}
			}

			CloseHandle( h );
		}

		if ( overlapped.hEvent ) {
			CloseHandle( overlapped.hEvent );
		}

		RingDestroy( ring );
	}

	ShowWindow( me->progress, SW_HIDE );
	ShowWindow( me->buttonCancel, SW_HIDE );

	ShowWindow( me->buttonGo, SW_SHOW );
	ShowWindow( me->editBinPath, SW_SHOW );
	ShowWindow( me->labelBinPath, SW_SHOW );
	ShowWindow( me->editRangeMax, SW_SHOW );
	ShowWindow( me->labelRangeMax, SW_SHOW );
	ShowWindow( me->editRangeMin, SW_SHOW );
	ShowWindow( me->labelRangeMin, SW_SHOW );

	EnableWindow( me->buttonGo, TRUE );
	EnableWindow( me->editBinPath, TRUE );
	EnableWindow( me->labelBinPath, TRUE );
	EnableWindow( me->editRangeMax, TRUE );
	EnableWindow( me->labelRangeMax, TRUE );
	EnableWindow( me->editRangeMin, TRUE );
	EnableWindow( me->labelRangeMin, TRUE );

	CloseHandle( searchInfo->thread );

	#undef PACKET_MEM_CALLSTACK
	#undef PACKET_MEM_FILELINE
	#undef PACKET_MEM_FREE
	#undef PACKET_MEM_ALLOC
	#undef SYSTEM_MEMORY

	return 0;
}

/*
========================
myCompareProc
========================
*/
static int CALLBACK myCompareProc( LPARAM a, LPARAM b, LPARAM param ) {
	const extendedAllocInfo_t * const aInfo = ( const extendedAllocInfo_t* )a;
	const extendedAllocInfo_t * const bInfo = ( const extendedAllocInfo_t* )b;
	addrHistoryUI_t const me = ( addrHistoryUI_t )param;

	const int sortAscending[ 3 ] = { 1, 0, -1 };
	const int sortDescending[ 3 ] = { -1, 0, 1 };

	const int * const sort = reportColumn[ me->sortOnColumn ].sort ? sortAscending : sortDescending;

	switch ( me->sortOnColumn ) {
		case 0: /* State */
			if ( aInfo->state == bInfo->state ) {
				return sort[ 1 ];
			}
			if ( aInfo->state > bInfo->state ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];

		case 1: /* Time */
			if ( aInfo->allocInfo.time == bInfo->allocInfo.time ) {
				return sort[ 1 ];
			}
			if ( aInfo->allocInfo.time > bInfo->allocInfo.time ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];

		case 2: /* Heap */
			if ( aInfo->allocInfo.heapID == bInfo->allocInfo.heapID ) {
				return sort[ 1 ];
			}
			if ( aInfo->allocInfo.heapID > bInfo->allocInfo.heapID ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];

		case 3: /* Actual Start */
			if ( aInfo->allocInfo.systemAddress == bInfo->allocInfo.systemAddress ) {
				return sort[ 1 ];
			}
			if ( aInfo->allocInfo.systemAddress > bInfo->allocInfo.systemAddress ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];

		case 4: { /* Actual End */
			const uint64_t aEnd = aInfo->allocInfo.systemAddress + aInfo->allocInfo.actualSize;
			const uint64_t bEnd = bInfo->allocInfo.systemAddress + bInfo->allocInfo.actualSize;
			if ( aEnd == bEnd ) {
				return sort[ 1 ];
			}
			if ( aEnd > bEnd ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];
		}

		case 5: /* Actual Size */
			if ( aInfo->allocInfo.actualSize == bInfo->allocInfo.actualSize ) {
				return sort[ 1 ];
			}
			if ( aInfo->allocInfo.actualSize > bInfo->allocInfo.actualSize ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];

		case 6: /* User Start */
			if ( aInfo->allocInfo.userAddress == bInfo->allocInfo.userAddress ) {
				return sort[ 1 ];
			}
			if ( aInfo->allocInfo.userAddress > bInfo->allocInfo.userAddress ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];

		case 7: { /* User End */
			const uint64_t aEnd = aInfo->allocInfo.userAddress + aInfo->allocInfo.requestedSize;
			const uint64_t bEnd = bInfo->allocInfo.userAddress + bInfo->allocInfo.requestedSize;
			if ( aEnd == bEnd ) {
				return sort[ 1 ];
			}
			if ( aEnd > bEnd ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];
		}

		case 8: /* User Size */
			if ( aInfo->allocInfo.requestedSize == bInfo->allocInfo.requestedSize ) {
				return sort[ 1 ];
			}
			if ( aInfo->allocInfo.requestedSize > bInfo->allocInfo.requestedSize ) {
				return sort[ 2 ];
			}
			return sort[ 0 ];

		case 9: { /* File */
			const char * const aFile = me->systemInterface->findStringByID( me->systemInterface, aInfo->allocInfo.file );
			const char * const bFile = me->systemInterface->findStringByID( me->systemInterface, bInfo->allocInfo.file );
			if ( aFile && bFile ) {
				return sort[ strcasecmp( aFile, bFile ) + 1 ];
			}
			if ( aFile ) {
				return -1;
			}
			if ( bFile ) {
				return 1;
			}
			return 0;
		}

		case 10: { /* Callstack */
			const int32_t end = min( aInfo->allocInfo.callstackDepth, bInfo->allocInfo.callstackDepth );
			int32_t i;

			for ( i = 0; i < end; ++i ) {
				if ( aInfo->allocInfo.callstack[ i ] > bInfo->allocInfo.callstack[ i ] ) {
					return sort[ 2 ];
				}
				if ( aInfo->allocInfo.callstack[ i ] < bInfo->allocInfo.callstack[ i ] ) {
					return sort[ 0 ];
				}
			}
			if ( aInfo->allocInfo.callstackDepth > bInfo->allocInfo.callstackDepth ) {
				return sort[ 2 ];
			}
			if ( aInfo->allocInfo.callstackDepth < bInfo->allocInfo.callstackDepth ) {
				return sort[ 0 ];
			}
			return sort[ 1 ];
		}
	}

    return sort[ 1 ];
}

/*
========================
myReportProc
========================
*/
static LRESULT CALLBACK myReportProc( HWND wnd, UINT msg, WPARAM wp, LPARAM lp ) {
	addrHistoryUI_t const me = ( addrHistoryUI_t )GetWindowLongPtr( wnd, GWLP_USERDATA );

	switch ( msg ) {
		case WM_PAINT: {
			HDC dc;
			PAINTSTRUCT ps;

			dc = BeginPaint( wnd, &ps );
			FillRect( dc, &ps.rcPaint, GetSysColorBrush( COLOR_3DFACE ) );			                                                                        
			EndPaint( wnd, &ps );
		} break;

		case WM_COMMAND: {
			switch ( HIWORD( wp ) ) {
				case BN_CLICKED:
					if ( ( HWND )lp == me->buttonGo ) {
						char text[ 256 ];

						searchInfo_t * const si = me->systemInterface->allocate( me->systemInterface, sizeof( searchInfo_t ) );

						if ( si == 0 ) {
							break;
						}

						si->me = me;

						if ( GetWindowTextA( me->editRangeMin, text, sizeof( text ) ) > 0 ) {
							if ( 1 != sscanf_s( text, "%" PRIx64, &si->addrMin ) ) {
								sscanf_s( text, "%" PRIu64, &si->addrMin );
							}
						}

						if ( GetWindowTextA( me->editRangeMax, text, sizeof( text ) ) > 0 ) {
							if ( 1 != sscanf_s( text, "%" PRIx64, &si->addrMax ) ) {
								sscanf_s( text, "%" PRIu64, &si->addrMax );
							}
						}

						if ( GetWindowTextA( me->editBinPath, si->path, sizeof( text ) ) <= 0 ) {
							snprintf( si->path, sizeof( si->path ), "%s", me->defaultBinPath );
						}

						si->thread = CreateThread( 0, 0, binSearch, si, 0, &si->threadID );
					} else if ( ( HWND )lp == me->buttonCancel ) {
						EnableWindow( me->buttonCancel, FALSE );
						me->cancelSearch = 1;
					}
					break;
			}
		} break;

		case WM_NOTIFY: {
			NMHDR *hdr = ( NMHDR* )lp;

			switch ( hdr->code ) {
				case LVN_COLUMNCLICK: {
					NMLISTVIEW* const nm = ( NMLISTVIEW* )lp;
					reportColumn[ nm->iSubItem ].sort ^= 1;
					me->sortOnColumn = nm->iSubItem;
					SendMessage( me->report, LVM_SORTITEMS, ( WPARAM )me, ( LPARAM )myCompareProc );
				} break;

#if 0
				case LVN_GETDISPINFO: {
					NMLVDISPINFO *info = ( NMLVDISPINFO* )hdr;
					if ( info->item.iItem < sizeof( blocks ) / sizeof( *blocks ) && info->item.iSubItem < reportColumnCount ) {
						info->item.pszText = blocks[ info->item.iItem ].string[ info->item.iSubItem ];
					}
				} break;
#endif /* 0 */
			}
		} break;
	}

	if ( NULL != me ) {
		return CallWindowProc( ( WNDPROC )me->prevProc, wnd, msg, wp, lp );
	}
	return 0;
}

/*
========================
myInitialize
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	LVCOLUMN	col;
	LOGFONTA	fnt;
	int32_t		i;

	addrHistoryUI_t me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->symbolInterface = me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_SYMBOL );

	me->list = List_Create( sizeof( extendedAllocInfo_t* ), 8 );

	me->wnd = me->systemInterface->createWindow( me->systemInterface, self, "Pointer History" );

	me->defaultBinPath = me->systemInterface->getBinFileName( me->systemInterface );

	memset( &fnt, 0, sizeof( fnt ) );
	strcpy( fnt.lfFaceName, "Tahoma" );
	fnt.lfHeight = FONT_HEIGHT;
	me->font = CreateFontIndirectA( &fnt );

	SetWindowLongPtr( me->wnd, GWLP_USERDATA, ( LONG_PTR )me );

	me->report = CreateWindowEx(	0,
									WC_LISTVIEW,
									"",
									WS_CHILD | WS_VISIBLE | LVS_REPORT,
									0,
									0,
									10,
									10,
									me->wnd,
									0,
									GetModuleHandle( 0 ),
									0 );
	SendMessageA( me->report, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->labelRangeMin = CreateWindowEx(	0,
										WC_STATICA,
										"Min",
										WS_CHILD | WS_VISIBLE,
										0,
										0,
										10,
										10,
										me->wnd,
										0,
										GetModuleHandle( 0 ),
										0 );
	SendMessageA( me->labelRangeMin, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->editRangeMin = CreateWindowEx(	0,
										WC_EDITA,
										"0x000000020083b0d8",
										WS_CHILD | WS_VISIBLE | WS_TABSTOP,
										0,
										0,
										10,
										10,
										me->wnd,
										0,
										GetModuleHandle( 0 ),
										0 );
	SendMessageA( me->editRangeMin, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->labelRangeMax = CreateWindowEx(	0,
										WC_STATICA,
										"Max",
										WS_CHILD | WS_VISIBLE,
										0,
										0,
										10,
										10,
										me->wnd,
										0,
										GetModuleHandle( 0 ),
										0 );
	SendMessageA( me->labelRangeMax, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->editRangeMax = CreateWindowEx(	0,
										WC_EDITA,
										"0x000000020083b0d8",
										WS_CHILD | WS_VISIBLE | WS_TABSTOP,
										0,
										0,
										10,
										10,
										me->wnd,
										0,
										GetModuleHandle( 0 ),
										0 );
	SendMessageA( me->editRangeMax, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->labelBinPath = CreateWindowEx(	0,
										WC_STATICA,
										"Bin Path",
										WS_CHILD | WS_VISIBLE | LVS_REPORT,
										0,
										0,
										10,
										10,
										me->wnd,
										0,
										GetModuleHandle( 0 ),
										0 );
	SendMessageA( me->labelBinPath, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->editBinPath = CreateWindowEx(	0,
										WC_EDITA,
										me->defaultBinPath,
										WS_CHILD | WS_VISIBLE | WS_TABSTOP,
										0,
										0,
										10,
										10,
										me->wnd,
										0,
										GetModuleHandle( 0 ),
										0 );
	SendMessageA( me->editBinPath, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->buttonGo = CreateWindowEx(	0,
									WC_BUTTONA,
									"Go",
									WS_CHILD | WS_VISIBLE | WS_TABSTOP,
									0,
									0,
									10,
									10,
									me->wnd,
									0,
									GetModuleHandle( 0 ),
									0 );
	SendMessageA( me->buttonGo, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->buttonCancel = CreateWindowEx(	0,
										WC_BUTTONA,
										"Cancel",
										WS_CHILD | WS_TABSTOP,
										0,
										0,
										10,
										10,
										me->wnd,
										0,
										GetModuleHandle( 0 ),
										0 );
	SendMessageA( me->buttonCancel, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->progress = CreateWindowEx(	0,
									PROGRESS_CLASS,
									0,
									WS_CHILD | PBS_SMOOTH,
									0,
									0,
									10,
									10,
									me->wnd,
									0,
									GetModuleHandle( 0 ),
									0 );
	SendMessageA( me->progress, WM_SETFONT, ( WPARAM )me->font, 1 );

	me->prevProc = GetWindowLongPtr( me->wnd, GWLP_WNDPROC );
	SetWindowLongPtr( me->wnd, GWLP_WNDPROC, ( LONG_PTR )myReportProc );

	col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	col.cx = 100;
	col.iSubItem = 0;
	col.fmt = LVCFMT_RIGHT;

	for ( i = 0; i < reportColumnCount; ++i ) {
		col.pszText = ( char* )reportColumn[ i ].name;
		col.fmt = reportColumn[ i ].columnFormat;
		SendMessageA( me->report, LVM_INSERTCOLUMNA, reportColumn[ i ].index,  (LPARAM )&col );
	}
}

/*
========================
myShutdown
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	addrHistoryUI_t const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->cancelSearch = 1;

	clearReport( me );

	DestroyWindow( me->progress );
	me->progress = NULL;

	DestroyWindow( me->buttonCancel);
	me->buttonCancel = NULL;

	DestroyWindow( me->buttonGo );
	me->buttonGo = NULL;

	DestroyWindow( me->editBinPath );
	me->editBinPath = NULL;

	DestroyWindow( me->labelBinPath );
	me->labelBinPath = NULL;

	DestroyWindow( me->editRangeMax );
	me->editRangeMax = NULL;

	DestroyWindow( me->labelRangeMax );
	me->labelRangeMax = NULL;

	DestroyWindow( me->editRangeMin );
	me->editRangeMin = NULL;

	DestroyWindow( me->labelRangeMin );
	me->labelRangeMin = NULL;

	DestroyWindow( me->report );
	me->report = NULL;

	me->systemInterface->destroyWindow( me->systemInterface, me->wnd );

	me->wnd = NULL;

	List_Destroy( me->list );
	me->list = NULL;
}

/*
========================
myResize
========================
*/
static void myResize( struct pluginInterface_type * const self ) {
	static const int CTL_BORDER = 2;
	static const int32_t fontWidth = ( int32_t )( FONT_HEIGHT * ( 9 / 16.0f ) );

	RECT r;
	int32_t len;
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;

	addrHistoryUI_t const me = pluginInterfaceToMe( self );

	GetClientRect( me->wnd, &r );

	y = CTL_BORDER;

	h = FONT_HEIGHT + CTL_BORDER * 2;

	len = GetWindowTextLength( me->buttonCancel );
	w = len * fontWidth + CTL_BORDER * 4;
	x = ( r.right - r.left ) - w;
	SetWindowPos( me->buttonCancel, 0, x, y, w, h, SWP_NOZORDER );

	w = x - CTL_BORDER;
	x = 0;
	SetWindowPos( me->progress, 0, x, y, w, h, SWP_NOZORDER );

	len = GetWindowTextLength( me->labelRangeMin );
	w = len * fontWidth + CTL_BORDER;
	SetWindowPos( me->labelRangeMin, 0, x, y, w, h, SWP_NOZORDER );
	x += w + CTL_BORDER;

	w = MAX_EDIT_NUMERIC_LENGTH * fontWidth + CTL_BORDER * 2;
	SetWindowPos( me->editRangeMin, 0, x, y, w, h, SWP_NOZORDER );
	x += w + CTL_BORDER;

	len = GetWindowTextLength( me->labelRangeMax );
	w = len * fontWidth + CTL_BORDER;
	SetWindowPos( me->labelRangeMax, 0, x, y, w, h, SWP_NOZORDER );
	x += w + CTL_BORDER;

	w = MAX_EDIT_NUMERIC_LENGTH * fontWidth + CTL_BORDER * 2;
	SetWindowPos( me->editRangeMax, 0, x, y, w, h, SWP_NOZORDER );
	x += w + CTL_BORDER;

	len = GetWindowTextLength( me->labelBinPath );
	w = len * fontWidth + CTL_BORDER;
	SetWindowPos( me->labelBinPath, 0, x, y, w, h, SWP_NOZORDER );
	x += w + CTL_BORDER;

	len = GetWindowTextLength( me->buttonGo );
	w = max( 1, ( r.right - r.left ) - ( x + CTL_BORDER + len * fontWidth + CTL_BORDER * 4 ) );
	SetWindowPos( me->editBinPath, 0, x, y, w, h, SWP_NOZORDER );
	x += w + CTL_BORDER;

	w = len * fontWidth + CTL_BORDER * 4;
	SetWindowPos( me->buttonGo, 0, x, y, w, h, SWP_NOZORDER );

	x = 0;
	y += h + CTL_BORDER;
	w = r.right - r.left;
	h = max( ( r.bottom - r.top ) - y, 1 );
	SetWindowPos( me->report, 0, x, y, w, h, SWP_NOZORDER );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	addrHistoryUI_t const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return me->name;
}

/*
========================
AddrHistoryUI_Create
========================
*/
struct pluginInterface_type * AddrHistoryUI_Create( struct systemInterface_type  * const sys ) {
	addrHistoryUI_t const me = ( addrHistoryUI_t )sys->allocate( sys, sizeof( addrHistoryUIData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( addrHistoryUIData_t ) );

	me->checksum = PLUGIN_ADDRHISTORYUI_CHECKSUM;

	me->pluginInterface.start = myStart;
	me->pluginInterface.stop = myStop;
	me->pluginInterface.resize = myResize;
	me->pluginInterface.getName = myGetName;

	me->systemInterface = sys;
	me->name = "AddrHistory_UI";

	return &me->pluginInterface;
}
