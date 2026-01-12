#/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "profile.h"
#include "time.h"
#include "plugin.h"
#include "../ctl_splitter.h"
#include "../ctl_tree.h"
#include "../list.h"
#include "../ctl_graph.h"

#define PLUGIN_PROFILEUI_CHECKSUM 0x50524f46494c5549 /* 'PROFILUI' */

#define MAX_THREADS 128
#define MAX_NAME_LENGTH 64
#define MAX_STRING_LENGTH 64
#define MAX_ACTIVE_FILTERS 16

#define COLUMN_LABEL -1
#define COLUMN_TOTALCOUNT 0
#define COLUMN_TOTALTIME 1
#define COLUMN_FRAMECOUNT 2
#define COLUMN_AVERAGETIME 3
#define COLUMN_MINSINGLETIME 4
#define COLUMN_MAXSINGLETIME 5

#define COMPARE_EPSILON_DOUBLE 0.000001

#define FRAME_OP_ENTER 0
#define FRAME_OP_LEAVE 1

#define FRAME_GROWTH_SIZE ( 256 * 1024 )

typedef struct _frameOpEnter_t {
	uint8_t		id;
	uint8_t		padding[ 7 ];
	uint64_t	threadID;
	uint64_t	time;
	uint64_t	groupMask;
	const char*	label;
} frameOpEnter_t;

typedef struct _frameOpLeave_t {
	uint8_t		id;
	uint8_t		padding[ 7 ];
	uint64_t	threadID;
	uint64_t	time;
} frameOpLeave_t;

typedef struct _frameOffset_t {
	size_t offset;
	size_t size;
} frameOffset_t;

typedef struct _plogBranch_t {
	const char*				label;
	struct _plogBranch_t*	parent;
	struct _plogBranch_t**	children;
	uintptr_t				treeviewItem;
	uint64_t				lastTime;
	uint64_t				groupMask;
	uint64_t				totalCount;
	uint64_t				totalTime;
	uint64_t				cachedFrameTime;
	uint64_t				cachedFrameCount;
	uint64_t				workingFrameTime;
	uint64_t				workingFrameMin;
	uint64_t				workingFrameMax;
	uint64_t				workingFrameCount;
	uint64_t				averageTime;
	uint64_t				minSingleTime;
	uint64_t				maxSingleTime;
	size_t					numChildren;
} plogBranch_t;

typedef enum _filterOp_t {
	FILTER_GREATER,
	FILTER_GREATEREQUAL,
	FILTER_EQUAL,
	FILTER_LESSEREQUAL,
	FILTER_LESSER,
	FILTER_SUBSTR,
} filterOp_t;

typedef enum _combineOp_t {
	COMBINE_AND,
	COMBINE_OR
} combineOp_t;

typedef struct _filterHeader_t {
	struct _profileUIData_t*	profileUI;
	HWND						removeButton;
	uint32_t					filter;
	filterOp_t					filterOp; /* g, ge, eq, le, l, substr */
	combineOp_t					combineOp;
	uint32_t					padding;
	void						( *destroy )( struct _filterHeader_t * const );
	void						( *render )( struct _filterHeader_t * const, rect_t * const ); /* copies the written rect back out */
	int32_t						( *update )( struct _filterHeader_t * const );
	void						( *apply_r )( struct _filterHeader_t * const, plogBranch_t * const, const int32_t includeSelf, const int32_t affectParents );
} filterHeader_t;

typedef struct _filterInstanceDouble_t {
	filterHeader_t	header;
	HWND			label;
	HWND			editBox;
	HWND			opList;
	double			value;
} filterInstanceDouble_t;

typedef struct _filterInstanceText_t {
	filterHeader_t	header;
	HWND			label;
	HWND			editBox;
	HWND			opList;
	HWND			removeButton;
	char			value[ MAX_STRING_LENGTH ];
} filterInstanceText_t;

typedef filterHeader_t* ( *filterCreateFunction )( struct _profileUIData_t * const, HWND, uint32_t filterIndex );

typedef struct _filter_t {
	const char*				name;
	filterCreateFunction	createFunction;
	int						column;
	uint32_t				padding;
} filter_t;

typedef struct _profileUIData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct systemInterface_type *	systemInterface;
	struct timeInterface_type *		timeInterface;
	struct graph_type				graph;
	HWND							wnd;
	HWND							filterAddLabel;
	HWND							filterAddList;
	OVERLAPPED						frameOutputOverlapped;
	uint64_t						frameOutputOffset;
	HANDLE							frameOutputHandle;
	uint8_t*						frameBuffer;
	size_t							frameBufferUsed;
	size_t							frameBufferSize;
	list_t							frameOffset;
	ctlTree_t						treeport;
	plogBranch_t*					treeRoot[ MAX_THREADS ];
	plogBranch_t*					treeTop[ MAX_THREADS ];
	uint64_t						treeThread[ MAX_THREADS ];
	uint64_t						lastSyncTime;
	size_t							numTreeThreads;
	graphSet_t						graphSetFrameTime;
	double							filterAverageMinMS;
	char							filterName[ MAX_NAME_LENGTH ];
	filterHeader_t*					activeFilter[ MAX_ACTIVE_FILTERS ];
	size_t							activeFilterCount;
	uint8_t							isFiltering;
	uint8_t							paused;
	uint8_t							writeInFlight;
	uint8_t							padding1[ 5 ];
} profileUIData_t;

static filterHeader_t* createFilterInstanceDouble( profileUIData_t * const, HWND, const uint32_t filterIndex );
static filterHeader_t* createFilterInstanceName( profileUIData_t * const, HWND, const uint32_t filterIndex );

static const filter_t availableFilters[] = {
	{ "Average (ms)",	createFilterInstanceDouble,	COLUMN_AVERAGETIME  },
	{ "Name",			createFilterInstanceName,	COLUMN_LABEL		},
};

static filterHeader_t* activeFilter[ MAX_ACTIVE_FILTERS ];
static size_t activeFilterCount = 0;

/*
========================
pluginInterfaceToMe
========================
*/
static profileUIData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_PROFILEUI_CHECKSUM ) {
			return ( profileUIData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
treeportGetBranchTextCB
========================
*/
static int treeportGetBranchTextCB( void * const self, void * const arg, char * const text, const size_t count ) {
	plogBranch_t * const branch = ( plogBranch_t* )arg;
	( void )self;
	snprintf( text, count, "%s", branch->label ? branch->label : "[unnamed branch]" );
	return 1;
}

/*
========================
treeportGetColumnTextCB
========================
*/
static int treeportGetColumnTextCB( void * const self, void * const arg, const uint32_t column, char * const text, const size_t count ) {
	profileUIData_t * const me = ( profileUIData_t* )self;
	plogBranch_t * const branch = ( plogBranch_t* )arg;
	double t;
	switch ( column ) {
		case COLUMN_TOTALCOUNT:
			snprintf( text, count, "%" PRIu64, branch->totalCount );
			break;

		case COLUMN_TOTALTIME:
			if ( me->timeInterface != 0 ) {
				t = me->timeInterface->convertTimeToMilliseconds( me->timeInterface, branch->totalTime );
			} else {
				t = ( double )branch->totalTime;
			}
			snprintf( text, count, "%.3f", t );
			break;

		case COLUMN_FRAMECOUNT:
			snprintf( text, count, "%" PRIu64, branch->cachedFrameCount );
			break;

		case COLUMN_AVERAGETIME:
			if ( me->timeInterface != 0 ) {
				t = me->timeInterface->convertTimeToMilliseconds( me->timeInterface, branch->averageTime );
			} else {
				t = ( double )branch->averageTime;
			}
			snprintf( text, count, "%.3f", t );
			break;

		case COLUMN_MINSINGLETIME:
			if ( me->timeInterface != 0 ) {
				t = me->timeInterface->convertTimeToMilliseconds( me->timeInterface, branch->minSingleTime );
			} else {
				t = ( double )branch->minSingleTime;
			}
			snprintf( text, count, "%.3f", t );
			break;

		case COLUMN_MAXSINGLETIME:
			if ( me->timeInterface != 0 ) {
				t = me->timeInterface->convertTimeToMilliseconds( me->timeInterface, branch->maxSingleTime );
			} else {
				t = ( double )branch->maxSingleTime;
			}
			snprintf( text, count, "%.3f", t );
			break;

		default:
			return 0;
	}

	return 1;
}

/*
========================
treeportSortCB
========================
*/
static int treeportSortCB( void * const self, const void * const paramA, const void * const paramB, unsigned int column ) {
	const plogBranch_t * const a = ( plogBranch_t* )paramA;
	const plogBranch_t * const b = ( plogBranch_t* )paramB;
	int rv = 0;

	( void )self;

	switch ( column ) {
		case COLUMN_LABEL:
			if ( a->label == 0 && b->label == 0 ) {
				rv = 0;
			} else if ( a->label == 0 ) {
				rv = 1;
			} else if ( b->label == 0 ) {
				rv = -1;
			} else {
				rv = stricmp( a->label, b->label );
			}
			break;

		case COLUMN_TOTALCOUNT:
			if ( a->totalCount < b->totalCount ) {
				rv = -1;
			} else if ( a->totalCount > b->totalCount ) {
				rv = 1;
			}
			break;

		case COLUMN_TOTALTIME:
			if ( a->totalTime < b->totalTime ) {
				rv = -1;
			} else if ( a->totalTime > b->totalTime ) {
				rv = 1;
			}
			break;

		case COLUMN_FRAMECOUNT:
			if ( a->cachedFrameCount < b->cachedFrameCount ) {
				rv = -1;
			} else if ( a->cachedFrameCount > b->cachedFrameCount ) {
				rv = 1;
			}
			break;

		case COLUMN_AVERAGETIME:
			if ( a->averageTime < b->averageTime ) {
				rv = -1;
			} else if ( a->averageTime > b->averageTime ) {
				rv = 1;
			}
			break;

		case COLUMN_MINSINGLETIME:
			if ( a->minSingleTime < b->minSingleTime ) {
				rv = -1;
			} else if ( a->minSingleTime > b->minSingleTime ) {
				rv = 1;
			}
			break;

		case COLUMN_MAXSINGLETIME:
			if ( a->maxSingleTime < b->maxSingleTime ) {
				rv = -1;
			} else if ( a->maxSingleTime > b->maxSingleTime ) {
				rv = 1;
			}
			break;

		default:
			break;
	}

	if ( rv == 0 ) {
		rv = ( int )( ( intptr_t )a - ( intptr_t )b );
	}

	return rv;
}

/*
========================
isubstr
  returns true if the search string contains the substring, zero otherwise.
  assumes that sub is already lowercase.
========================
*/
static int32_t isubstr( const char * const search, const char * const sub ) {
	char str[ MAX_STRING_LENGTH ];
	int i;

	if ( sub[ 0 ] == 0 ) {
		return 1;
	}

	strncpy( str, search, MAX_STRING_LENGTH );
	str[ sizeof( str ) - 1 ] = 0;

	for ( i = 0; str[ i ]; ++i ) {
		str[ i ] = ( char )tolower( str[ i ] );
	}

	return strstr( str, sub ) != 0;
}

/*
========================
createOpListCtl
========================
*/
static HWND createOpListCtl( HWND parent, const filterOp_t selected, const int32_t allowSubstr ) {
	HWND wnd;
	WPARAM i = 0;
	
	wnd = CreateWindowEx(	0,
							WC_COMBOBOX,
							"",
							WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
							0,
							0,
							10,
							64 * ( 5 + ( allowSubstr == 0 ? 0 : 1 ) ),
							parent,
							0,
							GetModuleHandle( 0 ),
							0);

	SendMessage( wnd, CB_INSERTSTRING, i++, ( LPARAM )">" );
	SendMessage( wnd, CB_INSERTSTRING, i++, ( LPARAM )">=" );
	SendMessage( wnd, CB_INSERTSTRING, i++, ( LPARAM )"==" );
	SendMessage( wnd, CB_INSERTSTRING, i++, ( LPARAM )"<=" );
	SendMessage( wnd, CB_INSERTSTRING, i++, ( LPARAM )"<" );

	if ( allowSubstr ) {
		SendMessage( wnd, CB_INSERTSTRING, i++, ( LPARAM )"substr" );
	}

	SendMessage( wnd, CB_SETDROPPEDWIDTH, 75, 0 );

	SendMessage( wnd, CB_SETCURSEL, ( WPARAM )selected, 0 );

	return wnd;
}

/*
========================
createLabelCtl
========================
*/
static HWND createLabelCtl( HWND parent, const char * const title ) {
	return CreateWindowExA(	0,
							WC_STATICA,
							title,
							WS_CHILD | WS_VISIBLE,
							0,
							0,
							10,
							10,
							parent,
							0,
							GetModuleHandle( 0 ),
							0);
}

/*
========================
createNumberEditCtl
========================
*/
static HWND createNumberEditCtl( HWND parent, const double value ) {
	char text[ 32 ];
	HWND wnd;

	_snprintf_s( text, sizeof( text ), _TRUNCATE, "%f", value );
	text[ sizeof( text ) - 1 ] = 0;

	wnd = CreateWindowExA(	WS_EX_CLIENTEDGE,
							WC_EDITA,
							text,
							WS_CHILD | WS_VISIBLE,
							0,
							0,
							10,
							10,
							parent,
							0,
							GetModuleHandle( 0 ),
							0);
	SendMessage( wnd, EM_LIMITTEXT, 8, 0 );
	return wnd;
}

/*
========================
createTextEditCtl
========================
*/
static HWND createTextEditCtl( HWND parent, const char * const value ) {
	HWND wnd = CreateWindowExA(	WS_EX_CLIENTEDGE,
								WC_EDITA,
								value,
								WS_CHILD | WS_VISIBLE,
								0,
								0,
								10,
								10,
								parent,
								0,
								GetModuleHandle( 0 ),
								0);
	SendMessage( wnd, EM_LIMITTEXT, MAX_STRING_LENGTH, 0 );
	return wnd;
}

/*
========================
createRemoveButton
========================
*/
static HWND createRemoveButton( HWND parent, filterHeader_t * const obj ) {
	HWND wnd = CreateWindowEx(	0,
								WC_BUTTON,
								"-",
								WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
								0,
								0,
								10,
								10,
								parent,
								0,
								GetModuleHandle( 0 ),
								0 );
	SendMessage( wnd, BM_SETSTATE, 0, 0 );

	SetWindowLongPtr( wnd, GWLP_USERDATA, ( LONG_PTR )obj );
	return wnd;
}

/*
-------------------------------------------------------------------------------
filter instance double
*/

/*
========================
renderFilterInstanceDouble
========================
*/
static void renderFilterInstanceDouble( filterHeader_t * const obj, rect_t * const r ) {
	filterInstanceDouble_t * const me = ( filterInstanceDouble_t* )obj;
	int ox;
	
	ox = 0;
	SetWindowPos( me->label, 0, r->left + ox, r->top + 1, 100, 16, SWP_NOZORDER );

	ox += 100;
	SetWindowPos( me->opList, 0, r->left + ox, r->top, 40, 18, SWP_NOZORDER );

	ox += 40;
	SetWindowPos( me->editBox, 0, r->left + ox, r->top, 100, 18, SWP_NOZORDER );

	ox += 100;

	r->bottom = r->top + 20;
	r->right = r->left + ox;
}

/*
========================
updateFilterInstanceDouble
========================
*/
static int32_t updateFilterInstanceDouble( filterHeader_t * const obj ) {
	filterInstanceDouble_t * const me = ( filterInstanceDouble_t* )obj;

	char text[ MAX_STRING_LENGTH ];
	char *dst;
	uint32_t i;
	DWORD start;
	DWORD end;
	DWORD orgStart;
	DWORD orgEnd;
	DWORD numDec;
	int modified;
	double value;

	obj->filterOp = ( filterOp_t )SendMessage( me->opList, CB_GETCURSEL, 0, 0 );

	SendMessage( me->editBox, EM_GETSEL, ( WPARAM )&start, ( LPARAM )&end );

	orgStart = start;
	orgEnd = end;

	GetWindowTextA( me->editBox, text, sizeof( text ) );
	text[ sizeof( text ) - 1 ] = 0;

	dst = text;
	numDec = 0;

	modified = 0;

	for ( i = 0; text[ i ] != 0; ++i ) {
		int copy = 0;

		switch ( text[ i ] ) {
			case '-':
				if ( i == 0 ) {
					copy = 1;
				}
				break;

			case '.':
				if ( numDec++ == 0 ) {
					copy = 1;
				}
				break;

			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				copy = 1;
				break;

			default:
				break;
		}

		if ( copy ) {
			*dst++ = text[ i ];
		} else {
			modified = 1;

			if ( start >= i ) {
				--start;
			}
			if ( end >= i ) {
				--end;
			}
		}
	}

	*dst = 0;

	if ( modified != 0 ) {
		SetWindowTextA( me->editBox, text );

		if ( start != orgStart || end != orgEnd ) {
			SendMessage( me->editBox, EM_SETSEL, ( WPARAM )start, ( LPARAM )end );
		}
	}

	value = atof( text );
	modified = fabs( value - me->value ) > 0.0000001;
	me->value = value;

	return modified;
}

/*
========================
applyFilterInstanceDouble_r
========================
*/
static void applyFilterInstanceDouble_r(	filterHeader_t * const obj,
											plogBranch_t * const branch,
											const int32_t includeSelf,
											const int32_t affectParents ) {
	filterInstanceDouble_t * const me = ( filterInstanceDouble_t* )obj;

	size_t i;
	int isVisible;
	
	if ( includeSelf == 0 ) {
		isVisible = 1;
	} else {
		const double t = obj->profileUI->timeInterface->convertTimeToMilliseconds( obj->profileUI->timeInterface, branch->averageTime );
		switch ( obj->filterOp ) {
			case FILTER_GREATER:
				isVisible = t > me->value;
				break;

			case FILTER_GREATEREQUAL:
				isVisible = t >= me->value;
				break;

			case FILTER_EQUAL:
				isVisible = fabs( t - me->value ) < COMPARE_EPSILON_DOUBLE;
				break;

			case FILTER_LESSEREQUAL:
				isVisible = t <= me->value;
				break;

			case FILTER_LESSER:
				isVisible = t < me->value;
				break;

			case FILTER_SUBSTR:
			default:
				isVisible = 0;
				break;
		}
	}

	if ( isVisible ) {
		ctlTreeAddVisibility( obj->profileUI->treeport, branch->treeviewItem, 1, affectParents );
	}

	for ( i = 0; i < branch->numChildren; ++i ) {
		plogBranch_t * const child = branch->children[ i ];
		if ( child != 0 ) {
			applyFilterInstanceDouble_r( obj, child, 1, affectParents );
		}
	}
}

/*
========================
destoryFilterInstanceDouble
========================
*/
static void destroyFilterInstanceDouble( filterHeader_t * const obj ) {
	filterInstanceDouble_t * const me = ( filterInstanceDouble_t* )obj;

	DestroyWindow( me->label );
	DestroyWindow( me->opList );
	DestroyWindow( me->editBox );

	obj->profileUI->systemInterface->deallocate( obj->profileUI->systemInterface, me );
}

/*
========================
createFilterInstanceDouble
========================
*/
static filterHeader_t* createFilterInstanceDouble(	profileUIData_t * const me,
													HWND wnd,
													const uint32_t filterIndex ) {
	filterInstanceDouble_t * const filter = me->systemInterface->allocate( me->systemInterface, sizeof( filterInstanceDouble_t ) );

	if ( filter == 0 ) {
		return 0;
	}

	filter->header.profileUI = me;
	filter->header.filter = filterIndex;
	filter->header.filterOp = FILTER_GREATEREQUAL;
	filter->header.combineOp = COMBINE_AND;
	filter->header.destroy = destroyFilterInstanceDouble;
	filter->header.render = renderFilterInstanceDouble;
	filter->header.update = updateFilterInstanceDouble;
	filter->header.apply_r = applyFilterInstanceDouble_r;

	filter->value = 0.0;

	filter->label = createLabelCtl( wnd, availableFilters[ filterIndex ].name );
	filter->opList = createOpListCtl( wnd, filter->header.filterOp, 0 );
	filter->editBox = createNumberEditCtl( wnd, filter->value );

	return &filter->header;
}

/*
filter instance double
-------------------------------------------------------------------------------
*/

/*
-------------------------------------------------------------------------------
filter instance name
*/

/*
========================
renderFilterInstanceName
========================
*/
static void renderFilterInstanceName( filterHeader_t * const obj, rect_t * const r ) {
	filterInstanceText_t * const me = ( filterInstanceText_t* )obj;
	int ox;
	
	ox = 0;
	SetWindowPos( me->label, 0, r->left + ox, r->top + 1, 100, 16, SWP_NOZORDER );

	ox += 100;
	SetWindowPos( me->opList, 0, r->left + ox, r->top, 40, 18, SWP_NOZORDER );

	ox += 40;
	SetWindowPos( me->editBox, 0, r->left + ox, r->top, 100, 18, SWP_NOZORDER );

	ox += 100;

	r->bottom = r->top + 22;
	r->right = r->left + ox;
}

/*
========================
updateFilterInstanceName
========================
*/
static int32_t updateFilterInstanceName( filterHeader_t * const obj ) {
	filterInstanceText_t * const me = ( filterInstanceText_t* )obj;
	char text[ MAX_STRING_LENGTH ];

	obj->filterOp = ( filterOp_t )SendMessage( me->opList, CB_GETCURSEL, 0, 0 );

	GetWindowTextA( me->editBox, text, sizeof( text ) );
	text[ sizeof( text ) - 1 ] = 0;

	strlwr( text );

	if ( 0 != strnicmp( text, me->value, MAX_STRING_LENGTH ) ) {
		strncpy( me->value, text, MAX_STRING_LENGTH );
		me->value[ MAX_STRING_LENGTH - 1 ] = 0;
		return 1;
	}

	return 0;
}

/*
========================
applyFilterInstanceName_r
========================
*/
static void applyFilterInstanceName_r(	filterHeader_t * const obj,
										plogBranch_t * const branch,
										const int32_t includeSelf,
										const int32_t affectParents ) {
	filterInstanceText_t * const me = ( filterInstanceText_t* )obj;

	size_t i;
	int isVisible;

	if ( branch == 0 || branch->label == NULL ) {
		return;
	}

	if ( includeSelf == 0 ) {
		isVisible = 1;
	} else {
		switch ( obj->filterOp ) {
			case FILTER_GREATER:
				isVisible = stricmp( branch->label, me->value ) > 0;
				break;

			case FILTER_GREATEREQUAL:
				isVisible = stricmp( branch->label, me->value ) >= 0;
				break;

			case FILTER_EQUAL:
				isVisible = stricmp( branch->label, me->value ) == 0;
				break;

			case FILTER_LESSEREQUAL:
				isVisible = stricmp( branch->label, me->value ) <= 0;
				break;

			case FILTER_LESSER:
				isVisible = stricmp( branch->label, me->value ) < 0;
				break;

			case FILTER_SUBSTR:
				isVisible = isubstr( branch->label, me->value );
				break;

			default:
				isVisible = 0;
				break;
		}
	}

	if ( isVisible ) {
		ctlTreeAddVisibility( obj->profileUI->treeport, branch->treeviewItem, 1, affectParents );
	}

	for ( i = 0; i < branch->numChildren; ++i ) {
		plogBranch_t * const child = branch->children[ i ];
		if ( child != 0 ) {
			applyFilterInstanceName_r( obj, child, 1, affectParents );
		}
	}
}

/*
========================
destroyFilterInstanceName
========================
*/
static void destroyFilterInstanceName( filterHeader_t * const obj ) {
	filterInstanceText_t * const me = ( filterInstanceText_t* )obj;

	DestroyWindow( me->label );
	DestroyWindow( me->opList );
	DestroyWindow( me->editBox );

	obj->profileUI->systemInterface->deallocate( obj->profileUI->systemInterface, me );
}

/*
========================
createFilterInstanceName
========================
*/
static filterHeader_t* createFilterInstanceName(	profileUIData_t * const me,
													HWND wnd,
													const uint32_t filterIndex ) {
	filterInstanceText_t * const filter = me->systemInterface->allocate( me->systemInterface, sizeof( filterInstanceText_t ) );

	if ( filter == 0 ) {
		return 0;
	}

	filter->header.profileUI = me;
	filter->header.filter = filterIndex;
	filter->header.filterOp = FILTER_SUBSTR;
	filter->header.combineOp = COMBINE_AND;
	filter->header.destroy = destroyFilterInstanceName;
	filter->header.render = renderFilterInstanceName;
	filter->header.update = updateFilterInstanceName;
	filter->header.apply_r = applyFilterInstanceName_r;

	filter->value[ 0 ] = 0;

	filter->label = createLabelCtl( wnd, availableFilters[ filterIndex ].name );
	filter->opList = createOpListCtl( wnd, filter->header.filterOp, 1 );
	filter->editBox = createTextEditCtl( wnd, filter->value );

	return &filter->header;
}

/*
filter instance name
-------------------------------------------------------------------------------
*/

/*
========================
getThreadIndex
========================
*/
static size_t getThreadIndex( profileUIData_t * const me, const uint64_t threadID ) {
	plogBranch_t *entry;
	char text[ 64 ];
	size_t i;

	for ( i = 0; i < me->numTreeThreads; ++i ) {
		if ( me->treeThread[ i ] == threadID ) {
			if ( me->treeRoot[ i ] != NULL ) {
				return i;
			}
			break;
		}
	}

	if ( i == me->numTreeThreads ) {
		if ( me->numTreeThreads == MAX_THREADS ) {
			for ( i = 0; i < MAX_THREADS; ++i ) {
				if ( me->treeThread[ i ] == 0 ) {
					break;
				}
			}
		} else {
			i = me->numTreeThreads++;
		}
	}

	if ( i == MAX_THREADS ) {
		return MAX_THREADS;
	}

	entry = ( plogBranch_t* )me->systemInterface->allocate( me->systemInterface, sizeof( plogBranch_t ) );
	if ( entry == NULL ) {
		return MAX_THREADS;
	}

	me->treeThread[ i ] = threadID;

	me->treeRoot[ i ] = entry;
	me->treeTop[ i ] = entry;

	memset( entry, 0, sizeof( plogBranch_t ) );

	snprintf( text, sizeof( text), "Thread 0x%llx", threadID );
	entry->label = me->systemInterface->addString( me->systemInterface, text );

	entry->treeviewItem = ctlTreeInsertBranch( me->treeport, 0, entry, 1 ); 

	return i;
}

/* smoothRate is the percentage of time used to calculate the average for each frame of data */
static void finalizeFrame( plogBranch_t * const top, const double smoothRate ) {
	size_t j;

	for ( j = 0; j < top->numChildren; ++j ) {
		plogBranch_t * const child = top->children[ j ];

		child->cachedFrameTime = child->workingFrameTime;
		child->cachedFrameCount = child->workingFrameCount;

		child->minSingleTime = ( child->workingFrameCount > 0 ) * child->workingFrameMin;
		child->maxSingleTime = ( child->workingFrameCount > 0 ) * child->workingFrameMax;

		child->workingFrameTime = 0;
		child->workingFrameCount = 0;

		child->workingFrameMin = ( uint64_t )-1;
		child->workingFrameMax = 0;

		child->averageTime = ( uint64_t )( ( child->averageTime * ( 1.0 - smoothRate ) ) + ( child->cachedFrameTime * smoothRate ) );

		finalizeFrame( child, smoothRate );
	}
}

/*
========================
onSync
========================
*/
static void onSync( void * const param, const uint64_t time ) {
	profileUIData_t * const me = ( profileUIData_t* )param;

	size_t i;

	( void )time;

	/* sum the times of all children and push them up to the root node (the thread) */
	for ( i = 0; i < MAX_THREADS; ++i ) {
		plogBranch_t * const branch = me->treeRoot[ i ];
		if ( branch != NULL ) {
			size_t n;
			uint64_t totalTime = 0;
			uint64_t cachedTime = 0;

			for ( n = 0; n < branch->numChildren; ++n ) {
				totalTime += branch->children[ n ]->totalTime;
				cachedTime += branch->children[ n ]->cachedFrameTime;
			}

			branch->totalTime = totalTime;
			branch->cachedFrameCount = 1;
			branch->cachedFrameTime = cachedTime;
		}
	}

	for ( i = 0; i < MAX_THREADS; ++i ) {
		plogBranch_t * const branch = me->treeRoot[ i ];
		if ( branch != NULL ) {
			finalizeFrame( branch, 0.01 );
		}
		me->treeTop[ i ] = branch;
	}
}

/*
========================
onEnter
========================
*/
static void onEnter(	void * const param,
						const uint64_t threadID,
						const uint64_t enterTime,
						const uint64_t groupMask,
						const char * const label ) {
	profileUIData_t * const me = ( profileUIData_t* )param;
	plogBranch_t *top;
	size_t threadIndex;
	size_t i;
	plogBranch_t *entry;
	plogBranch_t **list;

	( void )enterTime;
	
	threadIndex = getThreadIndex( me, threadID );
	top = me->treeTop[ threadIndex ];

	/* if top is empty, it means that someone popped the top of the tree off the stack.
	   this could be a corrupt packet or missing push.  we must choose to either ignore
	   all entries from this branch or corrupt the tree and start over again.  i choose
	   the latter... it's better to have misplaced information in the tree than none. */
	if ( top == 0 ) {
		me->treeTop[ threadIndex ] = me->treeRoot[ threadIndex ];
		top = me->treeRoot[ threadIndex ];

		if ( top == 0 ) {
			return;
		}
	}

	for ( i = 0; i < top->numChildren; ++i ) {
		if ( top->children[ i ] == 0 ) {
			continue;
		}
		if ( top->children[ i ]->label == label ) {
			break;
		}
	}

	if ( i == top->numChildren ) {
		/* add new item */
		list = top->children;
		top->children = ( plogBranch_t** )me->systemInterface->allocate( me->systemInterface, sizeof( plogBranch_t* ) * ( i + 1 ) );

		if ( top->children == 0 ) {
			top->children = list;
			return;
		}

		if ( list != 0 ) {
			memcpy( top->children, list, sizeof( plogBranch_t* ) * top->numChildren );
			me->systemInterface->deallocate( me->systemInterface, list );
		}

		entry = ( plogBranch_t* )me->systemInterface->allocate( me->systemInterface, sizeof( plogBranch_t ) );
		
		memset( entry, 0, sizeof( plogBranch_t ) );

		top->children[ top->numChildren++ ] = entry;

		entry->label = label;
		entry->parent = top;
		entry->treeviewItem = ctlTreeInsertBranch( me->treeport, top->treeviewItem, entry, 1 );
	} else {
/*		size_t j; */

		/* update existing item */
		entry = top->children[ i ];

#if 0
		/* clear child times */
		for ( j = 0; j < entry->numChildren; ++j ) {
			plogBranch_t * const child = entry->children[ j ];

			child->cachedFrameTime = child->workingFrameTime;
			child->cachedFrameCount = child->workingFrameCount;

			child->minSingleTime = ( child->workingFrameCount > 0 ) * child->workingFrameMin;
			child->maxSingleTime = ( child->workingFrameCount > 0 ) * child->workingFrameMax;

			child->workingFrameTime = 0;
			child->workingFrameCount = 0;

			child->workingFrameMin = ( uint64_t )-1;
			child->workingFrameMax = 0;

			child->averageTime = child->cachedFrameTime;/*( uint64_t )( ( child->averageTime * 0.99 ) + ( child->cachedFrameTime * 0.01 ) );*/
		}
#endif
	}

	me->treeTop[ threadIndex ] = entry;

	entry->groupMask |= groupMask;
	entry->lastTime = enterTime;
	++entry->workingFrameCount;
	++entry->totalCount;
}

/*
========================
onPop
========================
*/
static void onLeave( void * const param, const uint64_t threadID, const uint64_t time ) {
	profileUIData_t * const me = ( profileUIData_t* )param;
	plogBranch_t *top;
	size_t i;

	( void )threadID;

	i = getThreadIndex( me, threadID );

	top = me->treeTop[ i ];
	if ( top != 0 ) {
		const uint64_t delta = time - top->lastTime;

		if ( top->parent == NULL ) {
			int unused = 0;
			( void )unused;
			return;
		}

		top->totalTime += delta;
		top->workingFrameTime += delta;
		top->workingFrameMin = min( top->workingFrameMin, delta );
		top->workingFrameMax = max( top->workingFrameMax, delta );
		me->treeTop[ i ] = top->parent;
	}

	ctlTreeUpdate( me->treeport );
}

/*
========================
appendFrameData
========================
*/
static void appendFrameData( profileUIData_t * const me, const void * const pkt, const size_t size ) {
	const size_t nextSize = me->frameBufferUsed + size;

	if ( nextSize > me->frameBufferSize ) {
		const size_t alignedSize = ALIGN( nextSize, FRAME_GROWTH_SIZE );
		void * const newBlock = me->systemInterface->allocate( me->systemInterface, alignedSize );

		if ( newBlock == NULL ) {
			return;
		}

		if ( me->frameBuffer != NULL ) {
			memcpy( newBlock, me->frameBuffer, me->frameBufferSize );
			me->systemInterface->deallocate( me->systemInterface, me->frameBuffer );
		}

		me->frameBuffer = newBlock;
		me->frameBufferSize = alignedSize;
	}

	memcpy( me->frameBuffer + me->frameBufferUsed, pkt, size );
	me->frameBufferUsed += size;
}

/*
========================
onLiveSync
========================
*/
static void onLiveSync( void * const param, const uint64_t time ) {
	profileUIData_t * const me = ( profileUIData_t* )param;
	frameOffset_t offset;
	size_t left;
	DWORD wrote;

	if ( me->frameOutputHandle == INVALID_HANDLE_VALUE ) {
		return;
	}

	if ( me->writeInFlight ) {
		/* todo: diaf if not WAIT_OBJECT_0? */
		( void )WaitForSingleObject( me->frameOutputOverlapped.hEvent, INFINITE );
	}

	offset.offset = me->frameOutputOffset;
	offset.size = me->frameBufferUsed;

	List_Append( me->frameOffset, &offset );

	left = me->frameBufferUsed;
	while ( left != 0 ) {
		const DWORD toWrite = min( ( DWORD )left, UINT32_MAX );

		me->frameOutputOverlapped.Offset = ( DWORD )( me->frameOutputOffset & 0xffffffff );
		me->frameOutputOverlapped.OffsetHigh = ( DWORD )( me->frameOutputOffset >> 32 );

		( void )WriteFile(	me->frameOutputHandle,
							me->frameBuffer,
							toWrite,
							&wrote,
							&me->frameOutputOverlapped );

		me->writeInFlight |= 1;

		left -= toWrite;

		me->frameOutputOffset += toWrite;
	}

	me->frameBufferUsed = 0;

	if ( !me->paused ) {
		onSync( param, time );
	}

/*	if ( !me->paused ) { */
	{
		const uint64_t rawTime = time - me->lastSyncTime;
		const double msTime = me->timeInterface->convertTimeToMilliseconds( me->timeInterface, rawTime );
		Graph_SetValue( me->graph, me->graphSetFrameTime, msTime );
	}

	Graph_Step( me->graph );

	me->lastSyncTime = time;
}

/*
========================
onLiveEnter
========================
*/
static void onLiveEnter(	void * const param,
							const uint64_t threadID,
							const uint64_t time,
							const uint64_t groupMask,
							const char * const label ) {
	profileUIData_t * const me = ( profileUIData_t* )param;
	frameOpEnter_t op;

	op.id			= FRAME_OP_ENTER;
	op.threadID		= threadID;
	op.time			= time;
	op.groupMask	= groupMask;
	op.label		= label;
	appendFrameData( me, &op, sizeof( op ) );

	if ( !me->paused ) {
		onEnter( param, threadID, time, groupMask, label );
	}
}

/*
========================
onLiveLeave
========================
*/
static void onLiveLeave( void * const param, const uint64_t threadID, const uint64_t time ) {
	profileUIData_t * const me = ( profileUIData_t* )param;
	frameOpLeave_t op;

	op.id		=	FRAME_OP_LEAVE;
	op.threadID	= threadID;
	op.time		= time;
	appendFrameData( me, &op, sizeof( op ) );

	if ( !me->paused ) {
		onLeave( param, threadID, time );
	}
}

/*
========================
destroyTree_r
========================
*/
static void destroyTree_r( profileUIData_t * const me, plogBranch_t * const top ) {
	size_t i ;

	for ( i = 0; i < top->numChildren; ++i ) {
		if ( top->children[ i ] != 0 ) {
			destroyTree_r( me, top->children[ i ] );
		}
	}

	me->systemInterface->deallocate( me->systemInterface, top );
}

/*
========================
clearTree
========================
*/
static void clearTree( profileUIData_t * const me ) {
	size_t i;

	for ( i = 0; i < MAX_THREADS; ++i ) {
		plogBranch_t * const branch = me->treeRoot[ i ];
		if ( branch != NULL ) {
			destroyTree_r( me, branch );
			me->treeRoot[ i ] = 0;
		}
	}

	ctlTreeClear( me->treeport );
}

/*
========================
onFrameSelect
========================
*/
static void onFrameSelect( void * const param, const graphSet_t set, const size_t first, const size_t last ) {
	profileUIData_t * const me = ( profileUIData_t* )param;

	if ( first == ~0ULL && last == ~0ULL ) {
		return;
	}

	if ( set == me->graphSetFrameTime ) {
		const size_t count = List_GetNum( me->frameOffset );
		const size_t start = min( first, count );
		const size_t end = min( last, count );
		size_t i;
		size_t fileOffset;
		size_t readSize = 0;
		size_t remains;
		DWORD readBytes;
		OVERLAPPED overlap;
		uint8_t* buffer;

		me->paused = 1;

		fileOffset = SIZE_MAX;
		for ( i = start; i <= end; ++i ) {
			frameOffset_t * const offset = List_GetObject( me->frameOffset, i );
			debug_assert( offset != NULL );
			if ( offset != NULL ) {
				fileOffset = min( fileOffset, offset->offset );
				readSize += offset->size;
			}
		}

		memset( &overlap, 0, sizeof( overlap ) );

		overlap.hEvent = CreateEventA( NULL, FALSE, FALSE, NULL );
		debug_assert( overlap.hEvent != NULL );

		overlap.Offset = ( DWORD )( fileOffset & 0xffffffff );
		overlap.OffsetHigh = ( DWORD )( ( fileOffset >> 32 ) & 0xffffffff );

		buffer = me->systemInterface->allocate( me->systemInterface, readSize );

		remains = readSize;
		while ( remains != 0 ) {
			( void )ReadFile(	me->frameOutputHandle,
								buffer + ( readSize - remains ),
								( DWORD )remains,
								&readBytes,
								&overlap );
			readBytes = 0;
			( void )GetOverlappedResult( me->frameOutputHandle, &overlap, &readBytes, TRUE );
			remains -= readBytes;
		}

		clearTree( me );

		/* process the data */
		i = 0;
		while ( i < readSize ) {
			switch ( buffer[ i ] ) {
				case FRAME_OP_ENTER: {
					frameOpEnter_t * const op = ( frameOpEnter_t* )&buffer[ i ];
					onEnter( me, op->threadID, op->time, op->groupMask, op->label );
					i += sizeof( frameOpEnter_t );
				} break;

				case FRAME_OP_LEAVE: {
					frameOpLeave_t * const op = ( frameOpLeave_t* )&buffer[ i ];
					onLeave( me, op->threadID, op->time );
					i += sizeof( frameOpLeave_t );
				} break;

				default:
					debug_assert( 0 );
					i = readSize;
					break;
			}
		}

		me->systemInterface->deallocate( me->systemInterface, buffer );

		if ( overlap.hEvent ) {
			CloseHandle( overlap.hEvent );
		}

		for ( i = 0; i < MAX_THREADS; ++i ) {
			plogBranch_t * const branch = me->treeRoot[ i ];
			if ( branch != NULL ) {
				finalizeFrame( branch, 1.0 );
			}
			me->treeTop[ i ] = branch;
		}
	}
}

/*
========================
updateWindowLayout
========================
*/
static void updateWindowLayout( profileUIData_t * const me ) {
	RECT r;
	size_t i;
	rect_t r2;

	( void )me;

	GetClientRect( me->wnd, &r );

	/* render each filter */
	r2.left = r.left;
	r2.right = r.right;
	r2.top = r.top;
	r2.bottom = r.bottom;

	for ( i = 0; i < activeFilterCount; ++i ) {
		rect_t r3 = r2;
		activeFilter[ i ]->render( activeFilter[ i ], &r3 );
		SetWindowPos( activeFilter[ i ]->removeButton, 0, r3.right, r2.top, 16, r3.bottom - r3.top, SWP_NOZORDER );
		r2.top += ( r3.bottom - r3.top ) + 2;
	}

	SetWindowPos( me->filterAddLabel, 0, r2.left, r2.top + 1, 100, 18, SWP_NOZORDER );
	SetWindowPos( me->filterAddList, 0, r2.left + 100, r2.top, 175, 20, SWP_NOZORDER );
	r2.top += 24;

	/* tree report */
	r2.bottom -= 100;
	ctlTreeSetPos( me->treeport, &r2 );

	/* bar graph */
	r2.top = r2.bottom;
	r2.bottom += 100;
	Graph_SetPosition( me->graph, &r2 );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	const color_t green = { 0xff, 0, 0xff, 0 };
	profileUIData_t * const me = pluginInterfaceToMe( self );
	struct profileInterface_type * const profile = me ? me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_PROFILE ) : NULL;
	HMODULE mod = GetModuleHandle( 0 );
	char tempFileName[ PATH_MAX ];
	rect_t r;
	uint32_t i;

	if ( me == NULL ) {
		return;
	}

	me->frameOffset = List_Create( sizeof( frameOffset_t ), 1024 );

	me->timeInterface = ( struct timeInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_TIME );

	me->filterAverageMinMS = 0.0f;
	me->filterName[ 0 ] = 0;

	me->wnd = me->systemInterface->createWindow( me->systemInterface, self, "Profile" );

	r.left = 30;
	r.right = r.left + 10;
	r.top = 30;
	r.bottom = r.top + 10;
	me->treeport = ctlTreeCreate( me->wnd, &r, treeportGetBranchTextCB, me, treeportGetColumnTextCB, me );
	ctlTreeSetSortCallback( me->treeport, treeportSortCB, me );

	me->filterAddLabel = createLabelCtl( me->wnd, "Add filter:" );
	me->filterAddList = CreateWindowEx(	0,
										WC_COMBOBOX,
										"",
										WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
										0,
										0,
										100,
										1024,
										me->wnd,
										0,
										mod,
										0);

	for ( i = 0; i < sizeof( availableFilters ) / sizeof( *availableFilters ); ++i ) {
		SendMessage( me->filterAddList, CB_INSERTSTRING, ( WPARAM )-1, ( LPARAM )availableFilters[ i ].name );
	}
	SendMessage( me->filterAddList, CB_SETDROPPEDWIDTH, 175, 0 );

	ctlTreeInsertColumn( me->treeport, COLUMN_TOTALCOUNT, 75, "Total Count" );
	ctlTreeInsertColumn( me->treeport, COLUMN_TOTALTIME, 75, "Total (ms)" );
	ctlTreeInsertColumn( me->treeport, COLUMN_FRAMECOUNT, 75, "Frame Count" );
	ctlTreeInsertColumn( me->treeport, COLUMN_AVERAGETIME, 75, "Avg (ms)" );
	ctlTreeInsertColumn( me->treeport, COLUMN_MINSINGLETIME, 75, "Min (ms)" );
	ctlTreeInsertColumn( me->treeport, COLUMN_MAXSINGLETIME, 75, "Max (ms)" );

	me->graph = Graph_Create( me->systemInterface, me->wnd, &r );

	Graph_SetScale( me->graph, 1.0f, 16.666667 * 4 );
	me->graphSetFrameTime = Graph_AddSet( me->graph, "Frame", &green );

	Graph_RegisterMouseUp( me->graph, onFrameSelect, me );

	updateWindowLayout( me );

	if ( profile != 0 ) {
/*		profile->registerOnAddGroup( onAddGroup, me ); */
		profile->registerOnSync( profile, onLiveSync, me );
		profile->registerOnEnter( profile, onLiveEnter, me );
		profile->registerOnLeave( profile, onLiveLeave, me );
	}

	if ( me->frameOutputOverlapped.hEvent == NULL ) {
		me->frameOutputOverlapped.hEvent = CreateEvent( NULL, FALSE, TRUE, NULL );
	}

	if ( me->frameOutputHandle != INVALID_HANDLE_VALUE ) {
		CloseHandle( me->frameOutputHandle );
	}

	me->systemInterface->describeDataSource(	me->systemInterface,
												tempFileName,
												sizeof( tempFileName ),
												"remo.frame.%K.%P.%T.%R.txt" );
	tempFileName[ sizeof( tempFileName ) - 1 ] = 0;

	me->frameOutputHandle = CreateFile(	tempFileName,
										GENERIC_WRITE | GENERIC_READ,
										FILE_SHARE_READ,
										0,
										CREATE_ALWAYS,
										FILE_ATTRIBUTE_TEMPORARY |
										FILE_FLAG_DELETE_ON_CLOSE |
										FILE_FLAG_OVERLAPPED,
										NULL );

	/* this is necessary if we get two connections from the same device. for example, if a tool
	   and a game are run by the same exe, but both establish their own remo connections, this
	   will keep the data sources separate (otherwise the second open would fail and caus an
	   infinite hang on sync.) */
	i = 0;
	while ( me->frameOutputHandle == INVALID_HANDLE_VALUE ) {
		size_t len;
		me->systemInterface->describeDataSource(	me->systemInterface,
													tempFileName,
													sizeof( tempFileName ),
													"remo.frame.%K.%P.%T.%R" );
		tempFileName[ sizeof( tempFileName ) - 1 ] = 0;

		len = strlen( tempFileName );
		snprintf( tempFileName + len, sizeof( tempFileName ) - len, "%d.txt", i );
		tempFileName[ sizeof( tempFileName ) - 1 ] = 0;

		me->frameOutputHandle = CreateFile(	tempFileName,
											GENERIC_WRITE | GENERIC_READ,
											FILE_SHARE_READ,
											0,
											CREATE_ALWAYS,
											FILE_ATTRIBUTE_TEMPORARY |
											FILE_FLAG_DELETE_ON_CLOSE |
											FILE_FLAG_OVERLAPPED,
											NULL );

		i++;
	}
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	profileUIData_t * const me = pluginInterfaceToMe( self );
	struct profileInterface_type * const profile = me ? me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_PROFILE ) : NULL;
	size_t i;

	if ( me == NULL ) {
		return;
	}

	if ( profile != 0 ) {
		profile->unregisterOnLeave( profile, onLiveLeave, me );
		profile->unregisterOnEnter( profile, onLiveEnter, me );
		profile->unregisterOnSync( profile, onLiveSync, me );
/*		profile->unregisterOnAddGroup( profile, onAddGroup, me ); */
	}

	for ( i = 0; i < activeFilterCount; ++i ) {
		activeFilter[ i ]->destroy( activeFilter[ i ] );
		activeFilter[ i ] = 0;
	}

	if ( me->frameOutputHandle != INVALID_HANDLE_VALUE ) {
		CloseHandle( me->frameOutputHandle );
		me->frameOutputHandle = INVALID_HANDLE_VALUE;
	}

	if ( me->frameOutputOverlapped.hEvent != NULL ) {
		if ( me->writeInFlight ) {
			( void )WaitForSingleObject( me->frameOutputOverlapped.hEvent, INFINITE );
		}
		CloseHandle( me->frameOutputOverlapped.hEvent );
		me->frameOutputOverlapped.hEvent = NULL;
	}

	Graph_Destroy( me->graph );
	me->graph = NULL;

	if ( me->filterAddLabel != 0 ) {
		DestroyWindow( me->filterAddLabel );
		me->filterAddLabel = NULL;
	}

	if ( me->filterAddList != 0 ) {
		DestroyWindow( me->filterAddList );
		me->filterAddList = NULL;
	}

	ctlTreeDestroy( me->treeport );

	me->systemInterface->destroyWindow( me->systemInterface, me->wnd );

	me->systemInterface->deallocate( me->systemInterface, me->frameBuffer );

	List_Destroy( me->frameOffset );
}

/*
========================
myUpdate
========================
*/
static void myUpdate( struct pluginInterface_type * const self ) {
	profileUIData_t * const me = pluginInterfaceToMe( self );
	int32_t wasFiltering;
	size_t i;

	if ( me == NULL ) {
		return;
	}

	wasFiltering = me->isFiltering;

	/*
	  TODO: this is running full filters when it doesn't need to (either they haven't changed
	  or we're not about to render, so it's pointless.)
	*/

	me->isFiltering = activeFilterCount > 0;

	if ( SendMessage( me->filterAddList, CB_GETDROPPEDSTATE, 0, 0 ) == 0 ) {
		const int32_t sel = ( int )SendMessage( me->filterAddList, CB_GETCURSEL, 0, 0 );
		if ( sel != CB_ERR && activeFilterCount < MAX_ACTIVE_FILTERS ) {
			filterHeader_t * const hdr = availableFilters[ sel ].createFunction( me, me->wnd, sel );

			hdr->removeButton = createRemoveButton( me->wnd, hdr );

			activeFilter[ activeFilterCount++ ] = hdr;
			SendMessage( me->filterAddList, CB_SETCURSEL, ( WPARAM )-1, 0 );

			updateWindowLayout( me );
			InvalidateRect( me->wnd, 0, 0 );
		}
	}

	/* was filtering but isn't anymore - make everything visible. */
	if ( wasFiltering != 0 && me->isFiltering == 0 ) {
		ctlTreeSetVisibilityThreshold( me->treeport, 0 );
	}

	if ( me->isFiltering ) {
		int threshold = 0;

		ctlTreeSetVisibility( me->treeport, 0, 0, 1 );

		for ( i = 0; i < activeFilterCount; ++i ) {
			filterHeader_t * const header = activeFilter[ i ];
			if ( header->combineOp == COMBINE_AND ) {
				threshold++;
			}
			header->update( header );
		}

		for ( i = 0; i < activeFilterCount; ++i ) {
			filterHeader_t * const header = activeFilter[ i ];
			uint32_t j;

			ctlTreeAddVisibilitySeries( me->treeport, 1 );

			for ( j = 0; j < MAX_THREADS; ++j ) {
				plogBranch_t * const branch = me->treeRoot[ j ];
				if ( branch != NULL ) {
					header->apply_r( header, branch, 0, 1 );
				}
			}
		}

		ctlTreeSetVisibilityThreshold( me->treeport, threshold );
	}

	for ( i = 0; i < activeFilterCount; ++i ) {
		if ( BST_CHECKED != SendMessage( activeFilter[ i ]->removeButton, BM_GETSTATE, 0, 0 ) ) {
			continue;
		}

		ShowWindow( activeFilter[ i ]->removeButton, SW_HIDE );
		DestroyWindow( activeFilter[ i ]->removeButton );

		activeFilter[ i ]->destroy( activeFilter[ i ] );

		memcpy( &activeFilter[ i ], &activeFilter[ i + 1 ], sizeof( void* ) * ( activeFilterCount - ( i + 1 ) ) );

		--activeFilterCount;

		updateWindowLayout( me );
		InvalidateRect( me->wnd, 0, 0 );
	}
}

/*
========================
myRender
========================
*/
static void myRender( struct pluginInterface_type * const self ) {
	profileUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	ctlTreeRender( me->treeport );
	Graph_Render( me->graph );
/*	ctlTreeClear( me->treeport ); */
}

/*
========================
myResize
========================
*/
static void myResize( struct pluginInterface_type * const self ) {
	profileUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	updateWindowLayout( me );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return "Profile_UI";
}

/*
========================
MemGraphGetPluginInterface
========================
*/
struct pluginInterface_type * ProfileUI_Create( struct systemInterface_type * const sys ) {
	profileUIData_t * const me = ( profileUIData_t* )sys->allocate( sys, sizeof( profileUIData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( profileUIData_t ) );

	me->checksum = PLUGIN_PROFILEUI_CHECKSUM;

	me->pluginInterface.start			= myStart;
	me->pluginInterface.stop			= myStop;
	me->pluginInterface.update			= myUpdate;
	me->pluginInterface.render			= myRender;
	me->pluginInterface.resize			= myResize;
	me->pluginInterface.getName			= myGetName;

	me->systemInterface		= sys;
	me->frameOutputHandle	= INVALID_HANDLE_VALUE;

	return &me->pluginInterface;
}
