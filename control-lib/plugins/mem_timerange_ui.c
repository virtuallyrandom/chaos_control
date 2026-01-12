/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "memory.h"
#include "plugin.h"
#include "symbol.h"

#define PLUGIN_MEMTIMERANGE_CHECKSUM 0x4d454d544d524e /* 'MEMTMRN' */

typedef enum _memColumn_t {
	MEM_COLUMN_TIME,
	MEM_COLUMN_HEAP,
	MEM_COLUMN_SYSTEM_ADDRESS,
	MEM_COLUMN_USER_ADDRESS,
	MEM_COLUMN_REQUESTED_SIZE,
	MEM_COLUMN_ACTUAL_SIZE,
	MEM_COLUMN_FILE,
	MEM_COLUMN_LINE,
	MEM_COLUMN_TAG,
	MEM_COLUMN_ALIGN,
	MEM_COLUMN_CALLSTACK,

	MEM_COLUMN_MAX
} memColumn_t;

static const char* MEM_COLUMN_NAMES[] = {
	"Time",
	"Heap",
	"System Address",
	"User Address",
	"Requested",
	"Actual",
	"File",
	"Line",
	"Tag",
	"Align",
	"Callstack"
};

typedef struct _memTimeRangeUIData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct systemInterface_type *	systemInterface;
	struct memoryInterface_type *	memoryInterface;
	HWND							wnd;
	WNDPROC							parentWndProc;
	HWND							report;
	HWND							button;
	int								timeRangeIndex;
	int								padding;
	uint64_t						timeRange[ 2 ];
	void							( *timeOp )( struct _memTimeRangeUIData_t * const );
	FILE*							outputFile;
} memTimeRangeUIData_t;

/*
========================
nop
========================
*/
static void nop( memTimeRangeUIData_t * const me ) {
	( void )me;
}

/*
========================
pluginInterfaceToMe
========================
*/
static memTimeRangeUIData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_MEMTIMERANGE_CHECKSUM ) {
			return ( memTimeRangeUIData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
onMemoryEvent
========================
*/
static void onMemoryEvent( memTimeRangeUIData_t * const me, const struct allocInfo_type * const info ) {
	me->timeRange[ me->timeRangeIndex ] = info->time;
	me->memoryInterface->unregisterOnMemAlloc( me->memoryInterface, onMemoryEvent, me );
	me->memoryInterface->unregisterOnMemFree( me->memoryInterface, onMemoryEvent, me );
	me->timeOp( me );
}

/*
========================
allocCallback
========================
*/
static void allocCallback( memTimeRangeUIData_t * const me, const struct allocInfo_type * const info ) {
	LVITEM itm;
	char txt[ 4096 ];
	size_t txti = 0;
	int i;

	symbolInterface_t * const sym = ( symbolInterface_t* )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_SYMBOL );

	if ( info->time < me->timeRange[ 0 ] ) {
		return;
	}

	if ( info->time > me->timeRange[ 1 ] ) {
		return;
	}

	memset( &itm, 0, sizeof( itm ) );

	itm.mask = LVIF_TEXT;
	itm.pszText = txt;

	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%llx", info->time );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	itm.iItem = ( int )SendMessageA( me->report, LVM_INSERTITEMA, 0, ( LPARAM )&itm );

	/* heap */
	++itm.iSubItem;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%llx", info->heapID );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* system address */
	++itm.iSubItem;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%llx", info->systemAddress );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* user address */
	++itm.iSubItem;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "0x%llx", info->userAddress );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* requested */
	++itm.iSubItem;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%u", info->requestedSize );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* actual */
	++itm.iSubItem;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%u", info->actualSize);
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* file */
	++itm.iSubItem;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%s", me->systemInterface->findStringByID( me->systemInterface, info->file ) );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* line */
	++itm.iSubItem;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%u", info->line );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* tag */
	++itm.iSubItem;
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%hu", info->tag );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* align */
	++itm.iSubItem;
	_snprintf_s( txt,sizeof( txt ), _TRUNCATE,  "%hu", info->align );
	txt[ sizeof( txt ) - 1 ] = 0;
	if ( me->outputFile != NULL ) {
		fputs( txt, me->outputFile );
		fwrite( "\t", 1, 1, me->outputFile );
	}
	SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

	/* callstack */
	++itm.iSubItem;
	txti = 0;
	for ( i = 0; i < info->callstackDepth; ++i ) {
		const char *name;
		uint32_t line;

		if ( 0 == sym->find( sym, info->callstack[ i ], &name, 0, &line ) ) {
			txti += snprintf( txt + txti, sizeof( txt ) - txti, "0x%llx", info->callstack[ i ] );
		} else {
			txti += snprintf( txt + txti, sizeof( txt ) - txti, "%s:%u", name, line );
		}

		if ( i < info->callstackDepth - 1 ) {
			txti += snprintf( txt + txti, sizeof( txt ) - txti, " <- " );
		}
	}
	if ( txti > 0 ) {
		SendMessageA( me->report, LVM_SETITEMA, 0, ( LPARAM )&itm );

		if ( me->outputFile != NULL ) {
			fputs( txt, me->outputFile );
			fwrite( "\n", 1, 1, me->outputFile );
		}
	}
}

/*
========================
buildReport
========================
*/
static void buildReport( memTimeRangeUIData_t * const me ) {
	me->timeRangeIndex = -1;
	SetWindowTextA( me->button, "Start" );
	SendMessage( me->report, LVM_DELETEALLITEMS, 0, 0 );

	/* TODO: run a prepass on the alloc callback so we can build a linear allocator for data.
	   this will significantly speed up everything as we can manage strings, etc. ourselves
	   and not rely on the UI to do it for us. */
	me->outputFile = fopen( "timerange.txt", "w+" );
	if ( me->outputFile != NULL ) {
		size_t i;

		ShowWindow( me->report, SW_HIDE );
		for ( i = 0; i < sizeof( MEM_COLUMN_NAMES ) / sizeof( *MEM_COLUMN_NAMES ); ++i ) {
			fputs( MEM_COLUMN_NAMES[ i ], me->outputFile );
			fwrite( "\t", 1, 1, me->outputFile );
		}
		fwrite( "\n", 1, 1, me->outputFile );
		me->memoryInterface->walkHeap( me->memoryInterface, ( uint64_t )-1, allocCallback, me );
		ShowWindow( me->report, SW_SHOW );

		fclose( me->outputFile );
		me->outputFile = 0;
	}
}

/*
========================
startTracking
========================
*/
static void startTracking( memTimeRangeUIData_t * const me ) {
	SetWindowTextA( me->button, "Stop" );
	me->timeRangeIndex = 0;
	me->timeOp = nop;
	me->memoryInterface->registerOnMemAlloc( me->memoryInterface, onMemoryEvent, me );
	me->memoryInterface->registerOnMemFree( me->memoryInterface, onMemoryEvent, me );
}

/*
========================
stopTracking
========================
*/
static void stopTracking( memTimeRangeUIData_t * const me ) {
	SetWindowTextA( me->button, "Stopping..." );
	me->timeRangeIndex = 1;
	me->timeOp = buildReport;
	me->memoryInterface->registerOnMemAlloc( me->memoryInterface, onMemoryEvent, me );
	me->memoryInterface->registerOnMemFree( me->memoryInterface, onMemoryEvent, me );
}

/*
========================
myWndProc
========================
*/
static LRESULT CALLBACK myWndProc( HWND wnd, UINT msg, WPARAM wp, LPARAM lp ) {
	memTimeRangeUIData_t * const me = ( memTimeRangeUIData_t* )GetWindowLongPtr( wnd, GWLP_USERDATA );

	if ( me == NULL ) {
		return DefWindowProc( wnd, msg, wp, lp );
	}

	switch ( msg ) {
		case WM_COMMAND:
			if ( HIWORD( wp ) == BN_CLICKED && lp == ( LPARAM )me->button ) {
				if ( me->timeRangeIndex == -1 ) {
					startTracking( me );
				} else {
					stopTracking( me );
				}
			}
			break;

		default:
			break;
	}

	return CallWindowProc( me->parentWndProc, wnd, msg, wp, lp );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	uint32_t	i;
	LVCOLUMN	col;

	memTimeRangeUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->timeRangeIndex	= -1;
	me->timeOp			= nop;

	me->memoryInterface = me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_MEMORY );

	me->wnd = me->systemInterface->createWindow( me->systemInterface, self, "Memory (Time Range)" );

	me->parentWndProc = ( WNDPROC )GetWindowLongPtr( me->wnd, GWLP_WNDPROC );
	SetWindowLongPtr( me->wnd, GWLP_WNDPROC, ( LONG_PTR )myWndProc );
	SetWindowLongPtr( me->wnd, GWLP_USERDATA, ( LONG_PTR )me );

	me->button = CreateWindowEx(	0,
									WC_BUTTONA,
									"",
									WS_CHILD | WS_VISIBLE,
									0,
									0,
									10,
									10,
									me->wnd,
									0,
									GetModuleHandle( 0 ),
									0 );
	if ( !me->button ) {
		return;
	}

	me->timeRangeIndex = -1;
	me->timeOp = nop;
	SetWindowTextA( me->button, "Start" );

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
	if ( !me->report ) {
		return;
	}

	ListView_SetExtendedListViewStyle(	me->report,
										LVS_EX_FULLROWSELECT |
										LVS_EX_GRIDLINES |
										LVS_EX_DOUBLEBUFFER );


	col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	col.cx = 100;
	col.iSubItem = 0;

	col.fmt = LVCFMT_RIGHT;

	for ( i = 0; i < MEM_COLUMN_MAX; ++i ) {
		col.pszText = ( char* )MEM_COLUMN_NAMES[ i ];
		SendMessage( me->report, LVM_INSERTCOLUMN, ( WPARAM )++col.iSubItem,  ( LPARAM )&col );
	}

	InvalidateRect( me->report, 0, FALSE );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	memTimeRangeUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	DestroyWindow( me->report );

	me->systemInterface->destroyWindow( me->systemInterface, me->wnd );

	me->memoryInterface = 0;
	me->systemInterface = 0;
}

/*
========================
myResize
========================
*/
static void myResize( struct pluginInterface_type * const self ) {
	RECT r;
	int xo;
	int yo;

	memTimeRangeUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	GetClientRect( me->wnd, &r );

	xo = min( r.right - r.left, 200 );
	yo = min( r.bottom - r.top, GetSystemMetrics( SM_CYCAPTION ) );

	SetWindowPos(	me->button,
					0,
					0,
					0,
					xo,
					yo,
					SWP_NOZORDER );

	SetWindowPos(	me->report,
					0,
					0,
					yo,
					r.right - r.left,
					( r.bottom - r.top ) - yo,
					SWP_NOZORDER );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return "MemTimeRange";
}

/*
========================
MemTimeRangeUI_Create
========================
*/
struct pluginInterface_type * MemTimeRangeUI_Create( struct systemInterface_type * const sys ) {
	memTimeRangeUIData_t * const me = sys->allocate( sys, sizeof( memTimeRangeUIData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( memTimeRangeUIData_t ) );

	me->checksum = PLUGIN_MEMTIMERANGE_CHECKSUM;

	me->pluginInterface.start		= myStart;
	me->pluginInterface.stop		= myStop;
	me->pluginInterface.resize		= myResize;
	me->pluginInterface.getName		= myGetName;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
