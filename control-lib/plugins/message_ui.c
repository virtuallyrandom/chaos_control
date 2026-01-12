/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "message.h"
#include "plugin.h"

#define PLUGIN_MESSAGEUI_CHECKSUM 0x4d45535341475549 /* 'MESSAGUI' */

#define MESSAGE_BUFFER_GROWTH_STEP ( 4 * 1024 )
#define MESSAGE_LINE_GROWTH_STEP ( 256 )
#define MESSAGE_SPACES_PER_TAB 4

#define SCROLLBAR_WIDTH 16
#define EDIT_HEIGHT 14

#define C_COLOR_ESCAPE		'^'
#define C_COLOR_DEFAULT		'0'
#define C_COLOR_RED			'1'
#define C_COLOR_GREEN		'2'
#define C_COLOR_YELLOW		'3'
#define C_COLOR_BLUE		'4'
#define C_COLOR_CYAN		'5'
#define C_COLOR_MAGENTA		'6'
#define C_COLOR_WHITE		'7'
#define C_COLOR_GRAY		'8'
#define C_COLOR_BLACK		'9'
#define C_COLOR_DOOM_RED	'a'
#define C_COLOR_DOOM_BLUE	'b'
#define C_COLOR_DOOM_ORANGE	'c'
#define C_COLOR_GUI_YELLOW	'd'
#define C_COLOR_GUI_RED		'e'

#define MSG_FLAG_DIRTY      ( 1 << 0 )
#define MSG_FLAG_VSCROLLBAR ( 1 << 1 )
#define MSG_FLAG_HSCROLLBAR ( 1 << 2 )
#define MSG_FLAG_SCROLLLOCK ( 1 << 3 )
#define MSG_FLAG_SELECTION  ( 1 << 4 )

static const COLORREF LOOKUP_COLOR[] = {
	0xffffff, /* default */
	0x0000ff, /* red */
	0x00ff00, /* green */
	0x00ffff, /* yellow */
	0xff0000, /* blue */
	0xffff00, /* cyan */
	0xff00ff, /* magenta */
	0xffffff, /* white */
	0x777777, /* gray */
	0x333333, /* black */
	0x0000ff, /* doom red */
	0xff0000, /* doom blue */
	0xff7777, /* doom orange */
	0x00ffff, /* gui yellow */
	0x0000ff, /* gui red */
};

typedef struct _lineInfo_t {
	size_t	offset;
	size_t	length;
} lineInfo_t;

typedef struct _selectionInfo_t {
	size_t line;
	size_t offset;
} selectionInfo_t;

typedef struct _messageUIData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct systemInterface_type *	systemInterface;
	WNDPROC							prevWndProc;
	char *							buffer;
	size_t							bufferUsed;
	size_t							bufferCount;
	lineInfo_t *					line;
	size_t							lineComplete;
	size_t							lineCount;
	HWND							wnd;
	HWND							editCtl;
	HWND							vScrollbar;
	HFONT							font;
	HCURSOR							cursor;
	HDC								backBuffer;
	HBITMAP							backBitmap;
	uint32_t						backBitmapWidth;
	uint32_t						backBitmapHeight;
	uint16_t						characterHeight;
	uint16_t						characterWidth;
	uint32_t						flags;
	int32_t							vScrollbarPosition;
	int32_t							hScrollbarPosition;
	selectionInfo_t					selectionStart;
	selectionInfo_t					selectionEnd;
} messageUIData_t;

static messageUIData_t msgUIData;

/*
========================
pluginInterfaceToMe
========================
*/
static messageUIData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_MESSAGEUI_CHECKSUM ) {
			return ( messageUIData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
updateBitmap
========================
*/
static void updateBitmap( messageUIData_t * const me, HDC dc, RECT * const rect ) {
	uint32_t width	= rect->right - rect->left;
	uint32_t height	= rect->bottom - rect->top;
	HBRUSH brush = GetStockObject( BLACK_BRUSH );

	if ( width == me->backBitmapWidth && height == me->backBitmapHeight ) {
		return;
	}

	if ( me->backBitmap != INVALID_HANDLE_VALUE ) {
		DeleteObject( me->backBitmap );
	}

	me->backBitmap = CreateCompatibleBitmap( dc, width, height );

	SelectObject( me->backBuffer, me->backBitmap );

	FillRect( dc, rect, brush );
	FillRect( me->backBuffer, rect, brush );

	me->backBitmapWidth = width;
	me->backBitmapHeight = height;
}

/*
========================
onMessage
========================
*/
static void onMessage( void * const param, const char * const msg, const size_t length ) {
	messageUIData_t * const me = ( messageUIData_t * )param;
	lineInfo_t *line;
	size_t i;

	if ( me->bufferUsed + length > me->bufferCount ) {
		const size_t newCount = me->bufferCount + max( MESSAGE_BUFFER_GROWTH_STEP, length );
		const size_t newSize = newCount * sizeof( *me->buffer );
		char * const tmp = ( char * )me->systemInterface->allocate( me->systemInterface, newSize );
		memcpy( tmp, me->buffer, me->bufferUsed * sizeof( *me->buffer ) );
		me->systemInterface->deallocate( me->systemInterface, me->buffer );
		me->buffer = tmp;
		me->bufferCount = newCount;
	}

	line = me->line + me->lineComplete;

	for ( i = 0; i < length; ++i ) {
		if ( msg[ i ] == '\n' ) {
			if ( me->lineComplete + 1 == me->lineCount ) {
				const size_t newCount = me->lineCount + MESSAGE_LINE_GROWTH_STEP;
				const size_t newSize = newCount * sizeof( *me->line );
				lineInfo_t * const tmp = ( lineInfo_t * )me->systemInterface->allocate( me->systemInterface, newSize );
				memcpy( tmp, me->line, me->lineCount * sizeof( *me->line ) );
				me->systemInterface->deallocate( me->systemInterface, me->line );
				me->line = tmp;
				me->lineCount = newCount;
			}

			line = me->line + ++me->lineComplete;
			line->offset = me->bufferUsed + i;
			line->length = 0;
		} else {
			line->length++;
		}
	}

	memcpy( me->buffer + me->bufferUsed, msg, length );
	me->bufferUsed += length;

	me->flags |= MSG_FLAG_DIRTY;
}

/*
========================
updateContents
========================
*/
static void updateContents( messageUIData_t * const me, const RECT * const rectSrc ) {
	RECT rect = *rectSrc;
	size_t renderCount;
	size_t lineCount;
	size_t lineOffset;
	size_t i;
	SCROLLINFO si;
	lineInfo_t *line;

	if ( 0 == me->bufferUsed ) {
		return;
	}

	memset( &si, 0, sizeof( si ) );
	si.cbSize = sizeof( si );
	si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
	GetScrollInfo( me->vScrollbar, SB_CTL, &si );

	lineOffset = me->lineComplete - ( ( si.nPos + si.nPage ) / me->characterHeight );

	SelectObject( me->backBuffer, me->font );

	rect = *rectSrc;

	FillRect( me->backBuffer, &rect, GetStockObject( BLACK_BRUSH ) );
	SetBkMode( me->backBuffer, TRANSPARENT );

	renderCount = ( rect.bottom - rect.top ) / me->characterHeight;
	lineCount = renderCount;
	lineCount = min( lineCount, me->lineComplete );
	lineCount = min( me->lineComplete, lineCount + lineOffset );

	line = me->line + ( me->lineComplete - lineCount );

	rect.top = ( int )( rect.bottom - ( me->characterHeight * renderCount ) );

	for ( i = 0; i < renderCount; i++ ) {
		const int drawTextFlags = DT_LEFT | DT_SINGLELINE | DT_EXPANDTABS;

		static const COLORREF colorMask = 0x777777;
		const char *ptr = me->buffer + line->offset;
		const char *end = ptr + line->length + 1;

		RECT r = rect;

		// the line->length can be equal to bufferUsed, which puts the +1 beyond the end
		if ( end > me->buffer + me->bufferUsed ) {
			end = me->buffer + me->bufferUsed;
		}

		SetTextColor( me->backBuffer, LOOKUP_COLOR[ 0 ] );

		while ( ptr < end ) {
			const char *midStart = ptr;
			const char *midEnd = ptr;
			int trailingSpaces = 0;

			while ( midEnd < end ) {
				const size_t numColors = sizeof( LOOKUP_COLOR ) / sizeof( *LOOKUP_COLOR );
				if ( midEnd[ 0 ] == C_COLOR_ESCAPE && midEnd < end - 1 && midEnd[ 1 ] - C_COLOR_DEFAULT < numColors ) {
					const COLORREF color = LOOKUP_COLOR[ midEnd[ 1 ] - C_COLOR_DEFAULT ] & colorMask;
					RECT calcRect = r;
					int midLength = ( int )( midEnd - midStart - 1 );

					/* render everything up to this point */
					DrawTextA( me->backBuffer, midStart, midLength, &calcRect, drawTextFlags | DT_CALCRECT );
					DrawTextA( me->backBuffer, midStart, midLength, &r, drawTextFlags );

					r.left += calcRect.right - calcRect.left;
					r.left += trailingSpaces * me->characterWidth;

					trailingSpaces = 0;

					midEnd += 2;
					midStart = midEnd;

					SetTextColor( me->backBuffer, color );
				} else {
					if ( isspace( *midEnd ) ) {
						switch ( *midEnd ) {
							case ' ':
								trailingSpaces++;
								break;

							case '\t':
								trailingSpaces += MESSAGE_SPACES_PER_TAB;
								break;

							default:
								break;
						}
					}

					midEnd++;
				}
			}
			if ( midStart != midEnd ) {
				RECT calcRect = r;
				int midLength = ( int )( midEnd - midStart );

				/* render everything up to this point */
				DrawTextA( me->backBuffer, midStart, midLength, &calcRect, drawTextFlags | DT_CALCRECT );
				DrawTextA( me->backBuffer, midStart, midLength, &r, drawTextFlags );

				r.left += calcRect.right - calcRect.left;

				/* don't care about trailing whitespace here */
			}

			ptr = midEnd;
		}

		rect.top += me->characterHeight;

		line++;
	}
}

/*
========================
onMouseWheel
========================
*/
static void onMouseWheel( messageUIData_t * const me, WPARAM wp, LPARAM lp ) {
	SCROLLINFO si;
	int scrollSize;

	const int amt = GET_WHEEL_DELTA_WPARAM( wp ) / WHEEL_DELTA;

	( void )lp; /* mouse x/y in lo/hi word, respectively */

	memset( &si, 0, sizeof( si ) );
	si.cbSize = sizeof( si );
	si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
	GetScrollInfo( me->vScrollbar, SB_CTL, &si );

	/* each scroll "tick" is only a portion of a page, not a full page */
	scrollSize = ( int )si.nPage / 16;

	si.nPos = clamp( si.nMin, si.nMax - scrollSize, si.nPos - ( int )( scrollSize * amt ) );
	me->vScrollbarPosition = clamp( 0, ( int32_t )( me->lineComplete * me->characterHeight ), si.nPos );

	si.fMask = SIF_POS;
	SetScrollInfo( me->vScrollbar, SB_CTL, &si, TRUE );

	if ( si.nPos >= me->lineComplete * me->characterHeight - si.nPage ) {
		me->flags &= ~MSG_FLAG_SCROLLLOCK;
	} else {
		me->flags |= MSG_FLAG_SCROLLLOCK;
	}

	me->flags |= MSG_FLAG_DIRTY;
}

/*
========================
onVScroll
========================
*/
static void onVScroll( messageUIData_t * const me, const int32_t op ) {
	SCROLLINFO si;

	memset( &si, 0, sizeof( si ) );
	si.cbSize = sizeof( si );
	si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_TRACKPOS;
	GetScrollInfo( me->vScrollbar, SB_CTL, &si );

	switch ( op )
	{
		case SB_BOTTOM:
			break;

		case SB_ENDSCROLL:
			break;

		case SB_LINEDOWN:
			si.nPos = min( si.nMax, si.nPos + me->characterHeight );
			break;

		case SB_LINEUP:
			si.nPos = max( si.nMin, si.nPos - me->characterHeight );
			break;

		case SB_PAGEDOWN:
			si.nPos = min( si.nMax, si.nPos + ( int )si.nPage );
			break;

		case SB_PAGEUP:
			si.nPos = max( si.nMin, si.nPos - ( int )si.nPage );
			break;

		case SB_THUMBPOSITION:
			si.nPos = si.nTrackPos;
			break;

		case SB_THUMBTRACK:
			si.nPos = si.nTrackPos;
			me->flags |= MSG_FLAG_DIRTY;
			break;

		case SB_TOP:
			si.nPos = si.nMin;
			break;
	}

	me->vScrollbarPosition = clamp( 0, ( int32_t )( me->lineComplete * me->characterHeight ), si.nPos );

	si.fMask = SIF_POS;
	SetScrollInfo( me->vScrollbar, SB_CTL, &si, TRUE );

	if ( si.nPos >= me->lineComplete * me->characterHeight - si.nPage ) {
		me->flags &= ~MSG_FLAG_SCROLLLOCK;
	} else {
		me->flags |= MSG_FLAG_SCROLLLOCK;
	}

	me->flags |= MSG_FLAG_DIRTY;
}

/*
========================
myEditProc
========================
*/
static LRESULT CALLBACK myEditProc( HWND wnd, UINT msg, WPARAM wp, LPARAM lp ) {
	WNDPROC prev = ( WNDPROC )GetWindowLongPtr( wnd, GWLP_USERDATA );

	( void )lp;

	if ( msg == WM_CHAR && wp == VK_RETURN ) {
		HWND parent = GetParent( wnd );
		messageUIData_t * const me = ( messageUIData_t * )GetWindowLongPtr( parent, GWLP_USERDATA );
		char cmd[ PATH_MAX ];
		const int num = GetWindowTextA( me->editCtl, cmd, sizeof( cmd ) );
		me->systemInterface->sendPacket( me->systemInterface, 1, 1, cmd, num );
		SetWindowText( wnd, "" );
		SetFocus( wnd );
		return 0;
	}

	return CallWindowProc( prev, wnd, msg, wp, lp );
}

/*
========================
myWndProc
========================
*/
static LRESULT CALLBACK myWndProc( HWND wnd, UINT msg, WPARAM wp, LPARAM lp ) {
	messageUIData_t * const me = ( messageUIData_t * )GetWindowLongPtr( wnd, GWLP_USERDATA );

	switch ( msg ) {
		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			HDC dc;
			RECT r = { 0 };
			PAINTSTRUCT ps;

			int32_t pageSize;
			int32_t lineSize;
			int32_t linesPerPage;
			int32_t totalLines;

			SCROLLINFO si;

			dc = BeginPaint( wnd, &ps );

			GetClientRect( wnd, &r );
			r.bottom -= EDIT_HEIGHT;
			r.right -= SCROLLBAR_WIDTH;

			lineSize = me->characterHeight;
			pageSize = r.bottom - r.top;
			linesPerPage = pageSize / lineSize;
			totalLines = ( int32_t )me->lineComplete;

			updateBitmap( me, dc, &r );

			memset( &si, 0, sizeof( si ) );

			si.cbSize = sizeof( si );
			si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
			si.nMin = 0;
			si.nMax = totalLines * lineSize; /* convert to pixels */
			si.nPage = linesPerPage * lineSize; /* convert to pixels */

			if ( 0 == ( me->flags & MSG_FLAG_SCROLLLOCK ) ) {
				me->vScrollbarPosition = si.nMax;
			}

			si.nPos = clamp( 0, si.nMax, me->vScrollbarPosition );

			ShowWindow( me->vScrollbar, SW_SHOW );
			SetScrollInfo( me->vScrollbar, SB_CTL, &si, FALSE );

			if ( me->flags & MSG_FLAG_DIRTY ) {
				updateContents( me, &r );
				me->flags &= ~MSG_FLAG_DIRTY;
			}

			BitBlt( dc, 0, 0, me->backBitmapWidth, me->backBitmapHeight, me->backBuffer, 0, 0, SRCCOPY );
			                                                                        
			EndPaint( wnd, &ps );
		} return 0;

		case WM_SETCURSOR:
			if ( LOWORD( lp ) == HTCLIENT ) {
				SetCursor( me->cursor );
			}
			break;

#if (_WIN32_WINNT >= 0x0400) || (_WIN32_WINDOWS > 0x0400)
		case WM_MOUSEWHEEL:
			onMouseWheel( me, wp, lp );
			return 0;
#endif /* (_WIN32_WINNT >= 0x0400) || (_WIN32_WINDOWS > 0x0400) */

		case WM_VSCROLL:
			onVScroll( me, LOWORD( wp ) );
			break;
	}

	return CallWindowProc( me->prevWndProc, me->wnd, msg, wp, lp );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	messageUIData_t * const me = pluginInterfaceToMe( self );
	messageInterface_t*	msg;
	SCROLLINFO scrollInfo;
	HDC dc;
	LOGFONT fnt;
	RECT r;
	LONG_PTR proc;

	if ( me == NULL ) {
		return;
	}

	me->backBitmap = INVALID_HANDLE_VALUE;

	msg = me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_MESSAGE );

	if ( msg != 0 ) {
		msg->registerOnMessage( msg, onMessage, me );
	}

	me->wnd = me->systemInterface->createWindow( me->systemInterface, self, "Message" );

	GetClientRect( me->wnd, &r );

	me->editCtl = CreateWindowExA(	WS_EX_LEFT,
									"EDIT",
									NULL,
									WS_CHILD | WS_VISIBLE,
									0,
									r.bottom - EDIT_HEIGHT,
									r.right - SCROLLBAR_WIDTH + 2,
									EDIT_HEIGHT,
									me->wnd,
									0,
									GetModuleHandleA( 0 ),
									0 );
	proc = GetWindowLongPtr( me->editCtl, GWLP_WNDPROC );
	SetWindowLongPtr( me->editCtl, GWLP_USERDATA, proc );
	SetWindowLongPtr( me->editCtl, GWLP_WNDPROC, ( LONG_PTR )myEditProc );

	SendMessage( me->editCtl, EM_SETLIMITTEXT, PATH_MAX, 0 );

	me->vScrollbar = CreateWindowExA(	WS_EX_LEFT,
										"SCROLLBAR",
										NULL,
										WS_CHILD | WS_VISIBLE | SBS_VERT,
										r.right - SCROLLBAR_WIDTH,
										0,
										SCROLLBAR_WIDTH,
										r.bottom - r.top,
										me->wnd,
										0,
										GetModuleHandleA( 0 ),
										0 );

	me->buffer = ( char * )me->systemInterface->allocate( me->systemInterface, MESSAGE_BUFFER_GROWTH_STEP * sizeof( *me->buffer ) );
	me->bufferUsed = 0;
	me->bufferCount = MESSAGE_BUFFER_GROWTH_STEP;

	me->line = ( lineInfo_t * )me->systemInterface->allocate( me->systemInterface, MESSAGE_LINE_GROWTH_STEP * sizeof( *me->line ) );
	me->line[ 0 ].offset = 0;
	me->line[ 0 ].length = 0;
	me->lineComplete = 0;
	me->lineCount = MESSAGE_LINE_GROWTH_STEP;

	dc = GetDC( me->wnd );

	me->backBuffer = CreateCompatibleDC( dc );
	me->backBitmapWidth = 1;
	me->backBitmapHeight = 1;

	memset( &fnt, 0, sizeof( fnt ) );
	fnt.lfHeight = -MulDiv( 11, GetDeviceCaps( dc, LOGPIXELSY ), 72 );
	fnt.lfWeight = FW_NORMAL;
	fnt.lfPitchAndFamily = FIXED_PITCH;
	strcpy( fnt.lfFaceName, "Courier New" );
	me->font = CreateFontIndirect( &fnt );

	/* height of a character (maybe just do this once, eh?) */
	DrawTextA( me->backBuffer, "W", 1, &r, DT_CALCRECT );
	me->characterHeight = ( uint16_t )( r.bottom - r.top );
	me->characterWidth = ( uint16_t )( r.right - r.left );

	/* TODO: deprecated, use LoadImage */
	me->cursor = LoadCursor( NULL, IDC_IBEAM );

	GetClientRect( me->wnd, &r );
	updateBitmap( me, dc, &r );

	ReleaseDC( me->wnd, dc );

	scrollInfo.cbSize = sizeof( scrollInfo );
	scrollInfo.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
	scrollInfo.nMin = 0;
	scrollInfo.nMax = r.bottom - r.top;
	scrollInfo.nPage = r.bottom - r.top;
	scrollInfo.nPos = 0;
	scrollInfo.nTrackPos = 0;

	me->vScrollbarPosition = 0;

	SetScrollInfo( me->vScrollbar, SB_CTL, &scrollInfo, 1 );
	EnableScrollBar( me->vScrollbar, SB_CTL, ESB_ENABLE_BOTH );

	me->flags = MSG_FLAG_DIRTY;

	me->prevWndProc = ( WNDPROC )GetWindowLongPtr( me->wnd, GWLP_WNDPROC );
	SetWindowLongPtr( me->wnd, GWLP_WNDPROC, ( LONG_PTR )myWndProc );

	SetWindowLongPtr( me->wnd, GWLP_USERDATA, ( LONG_PTR )me );

	SetFocus( me->editCtl );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	messageUIData_t * const me = pluginInterfaceToMe( self );
	messageInterface_t *msg;

	if ( me == NULL ) {
		return;
	}

	DeleteObject( me->backBitmap );
	DeleteDC( me->backBuffer );
	DeleteObject( me->font );
	DestroyCursor( me->cursor );

	me->systemInterface->deallocate( me->systemInterface, me->buffer );
	me->systemInterface->deallocate( me->systemInterface, me->line );

	msg = me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_MESSAGE );
	if ( msg != 0 ) {
		msg->unregisterOnMessage( msg, onMessage, me );
	}

	me->systemInterface->destroyWindow( me->systemInterface, me->wnd );
}

/*
========================
myRender
========================
*/
static void myRender( struct pluginInterface_type * const self ) {
	messageUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	InvalidateRect( me->wnd, 0, 0 );
}

/*
========================
myResize
========================
*/
static void myResize( struct pluginInterface_type * const self ) {
	messageUIData_t * const me = pluginInterfaceToMe( self );
	RECT rect;

	if ( me == NULL ) {
		return;
	}

	GetClientRect( me->wnd, &rect );

	SetWindowPos(	me->vScrollbar,
					NULL,
					rect.right - SCROLLBAR_WIDTH,
					0,
					SCROLLBAR_WIDTH,
					rect.bottom - rect.top,
					SWP_NOZORDER );

	SetWindowPos(	me->editCtl,
					NULL,
					0,
					rect.bottom - EDIT_HEIGHT,
					rect.right - rect.left,
					EDIT_HEIGHT,
					SWP_NOZORDER );

	me->flags |= MSG_FLAG_DIRTY;
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return "Message";
}

/*
========================
MessageGetPluginInterface
========================
*/
struct pluginInterface_type * MessageUI_Create( struct systemInterface_type * const sys ) {
	messageUIData_t * const me = sys->allocate( sys, sizeof( messageUIData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( messageUIData_t ) );

	me->checksum = PLUGIN_MESSAGEUI_CHECKSUM;

	me->pluginInterface.start		= myStart;
	me->pluginInterface.stop		= myStop;
	me->pluginInterface.render		= myRender;
	me->pluginInterface.resize		= myResize;
	me->pluginInterface.getName		= myGetName;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
