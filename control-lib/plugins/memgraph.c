/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "memory.h"
#include "plugin.h"

#define PLUGIN_MEMGRAPH_CHECKSUM 0x4d454d4752415048 /* 'MEMGRAPH' */

typedef struct _memGraphData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct systemInterface_type *	systemInterface;
	HWND							wnd;
	WNDPROC							parentWndProc;
	HDC								backBuffer;
	HBITMAP							backBitmap;
	uint32_t						backBitmapWidth;
	uint32_t						backBitmapHeight;
	HPEN							penAlloc;
	HPEN							penFree;
	uint64_t						totalAlloc;
	uint64_t						totalRequested;
	uint64_t						addressMin;
	uint64_t						addressMax;
	int32_t							requestRedraw;
	int32_t							padding;
} memGraphData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static memGraphData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_MEMGRAPH_CHECKSUM ) {
			return ( memGraphData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
fillRange
========================
*/
static void fillRange(	memGraphData_t * const me,
						HPEN pen,
						const uint64_t minValue,
						const uint64_t maxValue,
						const uint64_t start,
						const uint64_t end ) {
	HGDIOBJ old = SelectObject( me->backBuffer, pen );

	const uint64_t range = maxValue - minValue;
	const uint64_t bytesPerRow = range / me->backBitmapHeight;
	const double pixelsPerByte = me->backBitmapWidth / (double)bytesPerRow;
	const double startValue = ( start - minValue ) * pixelsPerByte;
	const double endValue = ( end - minValue ) * pixelsPerByte;
	int startY = ( int )( startValue / me->backBitmapWidth );
	int startX = ( int )( startValue - startY * me->backBitmapWidth );
	int endY = ( int )( endValue / me->backBitmapWidth );
	int endX = ( int )( endValue - endY * me->backBitmapWidth );

	if ( startY == endY ) {
		MoveToEx( me->backBuffer, startX, startY, 0 );
		LineTo( me->backBuffer, max( endX, startX + 1), startY );
	} else {
		MoveToEx( me->backBuffer, startX, startY, 0 );
		LineTo( me->backBuffer, me->backBitmapWidth, startY );
		while ( startY < endY ) {
			MoveToEx( me->backBuffer, 0, startY, 0 );
			LineTo( me->backBuffer, me->backBitmapWidth, startY );
			startY++;
		}
		MoveToEx( me->backBuffer, 0, startY, 0 );
		LineTo( me->backBuffer, endX, startY );
	}

	SelectObject( me->backBuffer, old );
}

/*
========================
onAlloc
========================
*/
static void onAlloc( memGraphData_t * const me, const struct allocInfo_type * const info ) {
	const uint64_t wasMin = me->addressMin;
	const uint64_t wasMax = me->addressMax;

	const uint64_t start	= info->systemAddress;
	const uint64_t end		= info->systemAddress + info->actualSize;

	me->addressMin = min( start, me->addressMin );
	me->addressMax = max( end, me->addressMax );

	me->requestRedraw = me->requestRedraw || ( wasMin != me->addressMin || wasMax != me->addressMax );

	fillRange( me, me->penAlloc, me->addressMin, me->addressMax, start, end );
	me->totalAlloc += info->actualSize;
	me->totalRequested += info->requestedSize;
}

/*
========================
onFree
========================
*/
static void onFree( memGraphData_t * const me, const struct allocInfo_type * const info ) {
	fillRange( me, me->penFree, me->addressMin, me->addressMax, info->systemAddress, info->systemAddress + info->actualSize );
	me->totalAlloc -= info->actualSize;
	me->totalRequested -= info->requestedSize;
}

/*
========================
updateBitmap
========================
*/
static void updateBitmap( memGraphData_t * const me, HDC const dc, const RECT * const rect ) {
	uint32_t width;
	uint32_t height;

	width = rect->right - rect->left;
	height = rect->bottom - rect->top;

	if ( width == me->backBitmapWidth && height == me->backBitmapHeight ) {
		return;
	}

	if ( me->backBitmap != INVALID_HANDLE_VALUE ) {
		DeleteObject( me->backBitmap );
	}

	me->backBitmap = CreateCompatibleBitmap( dc, width, height );

	SelectObject( me->backBuffer, me->backBitmap );

	FillRect( dc, rect, GetStockObject( BLACK_BRUSH ) );
	FillRect( me->backBuffer, rect, GetStockObject( BLACK_BRUSH ) );

	me->backBitmapWidth = width;
	me->backBitmapHeight = height;
}

/*
========================
myWndProc
========================
*/
static LRESULT CALLBACK myWndProc( HWND wnd, UINT msg, WPARAM wp, LPARAM lp ) {
	memGraphData_t * const me = ( memGraphData_t* )GetWindowLongPtr( wnd, GWLP_USERDATA );

	if ( me == NULL ) {
		return DefWindowProc( wnd, msg, wp, lp );
	}

	switch ( msg ) {
		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			HDC dc;
			RECT r;
			PAINTSTRUCT ps;

			GetClientRect( wnd, &r );

			dc = BeginPaint( wnd, &ps );

			updateBitmap( me, dc, &r );

			BitBlt( dc, 0, 0, me->backBitmapWidth, me->backBitmapHeight, me->backBuffer, 0, 0, SRCCOPY );
			                                                                        
			EndPaint( wnd, &ps );
		} return 0;

		case WM_TIMER:
			InvalidateRect( wnd, 0, FALSE );
			return 0;
	}

	return CallWindowProc( me->parentWndProc, wnd, msg, wp, lp );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	HDC dc;
	RECT r;
	struct memoryInterface_type * mem;
	memGraphData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->backBitmap = INVALID_HANDLE_VALUE;

	mem = me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_MEMORY );

	if ( mem != 0 ) {
		mem->registerOnMemAlloc( mem, onAlloc, me );
		mem->registerOnMemFree( mem, onFree, me );
	}

	me->wnd = me->systemInterface->createWindow( me->systemInterface, self, "Memory Graph" );

	me->parentWndProc = ( WNDPROC )GetWindowLongPtr( me->wnd, GWLP_WNDPROC );
	SetWindowLongPtr( me->wnd, GWLP_WNDPROC, ( LONG_PTR )myWndProc );
	SetWindowLongPtr( me->wnd, GWLP_USERDATA, ( LONG_PTR )me );

	SetTimer( me->wnd, 1, 1000 / 30, 0 );

	dc = GetDC( me->wnd );
	me->backBuffer = CreateCompatibleDC( dc );
	me->backBitmapWidth = 1;
	me->backBitmapHeight = 1;
	ReleaseDC( me->wnd, dc );

	me->penAlloc = CreatePen( PS_SOLID, 1, RGB( 0, 196, 0 ) );
	me->penFree = CreatePen( PS_SOLID, 1, RGB( 0, 0, 196 ) );

	r.right = me->backBitmapWidth;
	r.bottom = me->backBitmapHeight;

	FillRect( me->backBuffer, &r, GetStockObject( BLACK_BRUSH ) );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	struct memoryInterface_type * mem;
	memGraphData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	mem = me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_MEMORY );
	if ( mem != 0 ) {
		mem->unregisterOnMemFree( mem, onFree, me );
		mem->unregisterOnMemAlloc( mem, onAlloc, me );
	}

	DeleteObject( me->penFree );
	DeleteObject( me->penAlloc );
	DeleteObject( me->backBitmap );
	DeleteDC( me->backBuffer );

	me->systemInterface->destroyWindow( me->systemInterface, me->wnd );
}


/*
========================
myUpdate
========================
*/
static void myUpdate( struct pluginInterface_type * const self ) {
	memGraphData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	if ( me->requestRedraw != 0 ) {
		InvalidateRect( me->wnd, 0, FALSE );
		me->requestRedraw = 0;
	}
}

/*
========================
myResize
========================
*/
static void myResize( struct pluginInterface_type * const self ) {
	memGraphData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->requestRedraw = 1;
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return "MemGraph";
}

/*
========================
MemGraph_Create
========================
*/
struct pluginInterface_type * MemGraph_Create( struct systemInterface_type * const sys ) {
	memGraphData_t * const me = sys->allocate( sys, sizeof( memGraphData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( memGraphData_t ) );

	me->checksum = PLUGIN_MEMGRAPH_CHECKSUM;

	me->pluginInterface.start		= myStart;
	me->pluginInterface.stop		= myStop;
	me->pluginInterface.update		= myUpdate;
	me->pluginInterface.resize		= myResize;
	me->pluginInterface.getName		= myGetName;

	me->systemInterface = sys;

	me->addressMin = ( uint64_t )-1;

	return &me->pluginInterface;
}
