/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "precompiled.h"

#include "memory.h"
#include "memory_summary_ui.h"
#include "plugin.h"
#include "symbol.h"

#pragma warning( push, 0 )
#include "../gui/imgui.h"
#pragma warning( pop )

#define PLUGIN_MEMORYSUMMARYUI_CHECKSUM 0x4d454d53554D5549 /* 'MEMSUMUI' */

typedef struct _memoryUsage_type {
	const char * name;
	uint64_t heap;
	uint64_t actualSize;
	uint64_t requestedSize;
	uint64_t activeCount;
	uint64_t totalCount;
	uint64_t hiWaterSize;
	uint64_t hiWaterCount;
} memoryUsage_type;

typedef struct _memoryUIData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct systemInterface_type *	systemInterface;
	memoryUsage_type				totalUsage;
	memoryUsage_type**				heap;
	size_t							heapCount;
} memoryUIData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static memoryUIData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_MEMORYSUMMARYUI_CHECKSUM ) {
			return ( memoryUIData_t* )checksum;
		}
	}
	return NULL;
}

static void gilobite( const uint64_t value, double * const result, const char ** const ext ) {
	double tmp = ( double )value;
	*ext = "";

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
findOrAddHeap
========================
*/
static memoryUsage_type * findOrAddHeap( memoryUIData_t * const me, const uint64_t heapID ) {
	for ( size_t i = 0; i < me->heapCount; ++i ) {
		if ( me->heap[ i ] && me->heap[ i ]->heap == heapID ) {
			return me->heap[ i ];
		}
	}

	memoryUsage_type ** const list = ( memoryUsage_type ** )me->systemInterface->allocate( me->systemInterface, sizeof( memoryUsage_type * ) * ( me->heapCount + 1 ) );

	me->heapCount++;

	if ( me->heap ) {
		memcpy( list, me->heap, sizeof( memoryUsage_type * ) * me->heapCount );
		me->systemInterface->deallocate( me->systemInterface, me->heap );
	}

	me->heap = list;

	memoryUsage_type * const heap = ( memoryUsage_type * )me->systemInterface->allocate( me->systemInterface, sizeof( memoryUsage_type ) );
	me->heap[ me->heapCount - 1 ] = heap;

	memset( heap, 0, sizeof( memoryUsage_type ) );
	heap->heap = heapID;

	return heap;
}

/*
========================
addUsage
========================
*/
static void addUsage( memoryUsage_type * const usage, const struct allocInfo_type * const info ) {
	usage->actualSize += info->actualSize;
	usage->requestedSize += info->requestedSize;
	usage->activeCount++;
	usage->totalCount++;
	usage->hiWaterSize = usage->hiWaterSize > usage->actualSize ? usage->hiWaterSize : usage->actualSize;
	usage->hiWaterCount = usage->hiWaterCount > usage->activeCount ? usage->hiWaterCount : usage->activeCount;
}

/*
========================
subUsage
========================
*/
static void subUsage( memoryUsage_type * const usage, const struct allocInfo_type * const info ) {
	usage->actualSize -= info->actualSize;
	usage->requestedSize -= info->requestedSize;
	usage->activeCount--;
}

/*
========================
onHeapCreate
========================
*/
static void onHeapCreate( void * const param, const struct heapInfo_type * const info ) {
	memoryUIData_t * const me = ( memoryUIData_t * )param;
	memoryUsage_type * const heap = findOrAddHeap( me, info->heapID );
	heap->name = info->name;
}

/*
========================
onMemAlloc
========================
*/
static void onMemAlloc( void * const param, const struct allocInfo_type * const info ) {
	memoryUIData_t * const me = ( memoryUIData_t * )param;
	memoryUsage_type * const heap = findOrAddHeap( me, info->heapID );
	addUsage( heap, info );
	addUsage( &me->totalUsage, info );
}

/*
========================
onMemFree
========================
*/
static void onMemFree( void * const param, const struct allocInfo_type * const info ) {
	memoryUIData_t * const me = ( memoryUIData_t * )param;
	memoryUsage_type * const heap = findOrAddHeap( me, info->heapID );
	subUsage( heap, info );
	subUsage( &me->totalUsage, info );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	struct memoryInterface_type *	mem;
	memoryUIData_t * const			me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	mem = ( struct memoryInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_MEMORY );

	if ( mem != NULL ) {
		mem->registerOnHeapCreate( mem, onHeapCreate, me );
		mem->registerOnMemAlloc( mem, onMemAlloc, me );
		mem->registerOnMemFree( mem, onMemFree, me );
	}
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	struct memoryInterface_type * mem;
	memoryUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	mem = ( struct memoryInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_MEMORY );
	if ( mem != NULL ) {
		mem->unregisterOnMemFree( mem, onMemFree, me );
		mem->unregisterOnMemAlloc( mem, onMemAlloc, me );
		mem->unregisterOnHeapCreate( mem, onHeapCreate, me );
	}
}

static const char * commify( char * const tmp, const size_t size, uint64_t value ) {
	char * end = tmp + size;

	end--;
	assert( end > tmp );
	*end = 0;
	assert( end > tmp );

	if ( value == 0 ) {
		*--end = '0';
	} else {
		while ( value > 1000 ) {
			uint64_t rem = value % 1000;
			for ( size_t i = 0; i < 3; i++ ) {
				assert( end > tmp );
				*--end = '0' + ( char )( rem % 10 );
				rem /= 10;
			}
			assert( end > tmp );
			*--end = ',';
			value /= 1000;
		}
		while ( value != 0 ) {
			assert( end > tmp );
			*--end = '0' + ( char )( value % 10 );
			value /= 10;
		}
	}
	return end;
}

static void renderUsageTooltip( const memoryUsage_type * const usage ) {
	double actualSize;
	const char * actualSizeExt;
	double hiWaterSize;
	const char * hiWaterSizeExt;
	gilobite( usage->actualSize, &actualSize, &actualSizeExt );
	gilobite( usage->hiWaterSize, &hiWaterSize, &hiWaterSizeExt );
	char tmp[ 3 ][ 32 ];
	const char * activeCountStr = commify( tmp[ 0 ], sizeof( tmp[ 0 ] ), usage->activeCount );
	const char * hiWaterCountStr = commify( tmp[ 1 ], sizeof( tmp[ 0 ] ), usage->hiWaterCount );
	const char * allocCountStr = commify( tmp[ 2 ], sizeof( tmp[ 0 ] ), usage->totalCount );
	const char * const name = usage->name ? usage->name : "[unnamed]";
	ImGui::SetTooltip(	"Heap: %s\n"
						"Current usage: %.2f%sB across %s allocations\n"
						"HiWater usage: %.2f%sB across %s allocations\n"
						"Total allocation count: %s",
						name,
						actualSize, actualSizeExt, activeCountStr,
						hiWaterSize, hiWaterSizeExt, hiWaterCountStr,
						allocCountStr );
}

static bool renderUsage( ImDrawList * const drawList, const ImVec2 & textPos, const ImVec2 & topLeft, const ImVec2 & bottomRight, const uint64_t maxSize, memoryUsage_type * const usage, const bool asTree ) {
	( void )asTree;

	const ImU32 requestedColor = ImGui::ColorConvertFloat4ToU32( ImVec4( .15f, .75f, .15f, 1 ) );
	const ImU32 actualColor = ImGui::ColorConvertFloat4ToU32( ImVec4( .15f, .15f, .75f, 1 ) );
	static ImU32 borderColor = ImGui::ColorConvertFloat4ToU32( ImVec4( .25f, .25f, .25f, 1 ) );
	const ImU32 nameColor = ImGui::ColorConvertFloat4ToU32( ImVec4( .5f, .5f, .5f, 1 ) );

	ImVec2 tl = topLeft;
	ImVec2 br = bottomRight;

	ImGui::SetNextWindowPos( tl );

	const char * const name = usage->name ? usage->name : "[unnamed]";
	const size_t size = strlen( name ) + 20;
	char * const uid = ( char * )alloca( size + 1 );
	_snprintf_s( uid, size, _TRUNCATE, "%s%p", name, usage );
	uid[ size ] = 0;
	ImGui::BeginChild( uid, ImVec2( br.x - tl.x, br.y - tl.y ), false, ImGuiWindowFlags_NoDecoration );

	drawList->AddText( textPos, nameColor, name );

	const float requestedPct = usage->requestedSize / ( float )maxSize;
	const float requestedX = tl.x + ( br.x - tl.x ) * requestedPct;

	const float actualPct = usage->actualSize / ( float )maxSize;
	const float actualX = tl.x + ( br.x - tl.x ) * actualPct;

	drawList->AddRect( tl, br, borderColor );

	tl.x += 1;
	tl.y += 1;

	br.x -= 1;
	br.y -= 1;

	drawList->AddRectFilled( tl, ImVec2( requestedX, br.y ), requestedColor );
	drawList->AddRectFilled( ImVec2( requestedX, tl.y ), ImVec2( actualX, br.y ), actualColor );

	// hiWaterSize
	{
		const float pct = usage->hiWaterSize / ( float )maxSize;
		const float xpos = tl.x + ( br.x - tl.x ) * pct;
		const ImVec2 a( xpos, tl.y );
		const ImVec2 b( xpos, br.y );
		drawList->AddLine( a, b, ImGui::ColorConvertFloat4ToU32( ImVec4( .50f, .25f, .25f, 1 ) ) );
	}

	ImGui::EndChild();

	if ( ImGui::IsItemHovered() ) {
		renderUsageTooltip( usage );
	}

	return true;
}

/*
========================
myRender
========================
*/
static void myRender( struct pluginInterface_type * const self ) {
	memoryUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}
	ImDrawList * const drawList = ImGui::GetWindowDrawList();
	const ImGuiStyle & style = ImGui::GetStyle();
	const float fontHeight = ImGui::GetFontSize();
	const float barHeight = fontHeight + style.WindowBorderSize * 2;
	const ImVec2 pos = ImGui::GetWindowPos();
	const ImVec2 size = ImGui::GetWindowSize();
	ImVec2 tl = ImGui::GetWindowContentRegionMin();
	ImVec2 br = ImGui::GetWindowContentRegionMax();
	tl.x += pos.x + style.WindowPadding.x * 2;
	tl.y += pos.y + style.WindowPadding.x * 2;
	br.x += pos.x - style.WindowPadding.x * 2;
	br.y = tl.y + barHeight;

	float maxTextWidth = ImGui::CalcTextSize( me->totalUsage.name ).x;
	for ( size_t i = 0; i < me->heapCount; i++ ) {
		memoryUsage_type * const usage = me->heap[ i ];
		const ImVec2 textSize = ImGui::CalcTextSize( usage->name );
		if ( textSize.x > maxTextWidth ) {
			maxTextWidth = textSize.x;
		}
	}

	ImVec2 textPos = tl;
	tl.x += maxTextWidth + 1;
	renderUsage( drawList, textPos, tl, br, me->totalUsage.hiWaterSize, &me->totalUsage, true );
	for ( size_t i = 0; i < me->heapCount; i++ ) {
		memoryUsage_type * const usage = me->heap[ i ];
		tl.y += barHeight + 1;
		br.y += barHeight + 1;
		textPos.y += barHeight + 1;
		renderUsage( drawList, textPos, tl, br, me->totalUsage.hiWaterSize, usage, false );
	}
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return "Memory Summary";
}

/*
========================
MemoryUI_Create
========================
*/
struct pluginInterface_type * MemorySummaryUI_Create( struct systemInterface_type * const sys ) {
	memoryUIData_t * const me = ( memoryUIData_t * )sys->allocate( sys, sizeof( memoryUIData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( memoryUIData_t ) );

	me->checksum = PLUGIN_MEMORYSUMMARYUI_CHECKSUM;

	me->pluginInterface.start	= myStart;
	me->pluginInterface.stop	= myStop;
	me->pluginInterface.render	= myRender;
	me->pluginInterface.getName	= myGetName;

	me->systemInterface = sys;

	me->totalUsage.name = "Total";

#if 0
	const struct heapInfo_type h[] = {
		{ "A Heap", 0, 0, 0, 0, 0, 0, 0 },
		{ "Another Heap", 1, 0, 0, 0, 0, 0, 0 },
		{ "Shh", 2, 0, 0, 0, 0, 0, 0 },
	};

	for ( size_t i = 0; i < sizeof( h ) / sizeof( h[ 0 ] ); i++ ) {
		onHeapCreate( me, h + i );
	}

	const uint32_t maxAllocOverhead = 64;
	const uint32_t maxAllocSize = 256;

	srand( 123456 );
	for ( size_t i = 0; i < 4096; i++ ) {
		const uint64_t addr =	( ( uint64_t )rand() |
								  ( uint64_t )rand() << 16 |
								  ( uint64_t )rand() << 32 ) + 7 & ~7;
		const uint32_t size = ( uint64_t )rand() & ( maxAllocSize - 1 );

		struct allocInfo_type a = {
			0,			// time
			rand() % ( sizeof( h ) / sizeof( h[ 0 ] ) ), // uint64_t	heapID;
			addr,		//uint64_t	systemAddress;
			addr + 8,	//uint64_t	userAddress;
			0,			//uint64_t*	callstack;
			size,		//uint32_t	requestedSize;
			size + ( rand() & ( maxAllocOverhead - 1 ) ), //uint32_t	actualSize;
			0,			//uint32_t	line;
			( uint16_t )-1,			//uint16_t	file;
			0,			//uint16_t	tag;
			0,			//uint16_t	align;
			0			//uint8_t		callstackDepth;
						//uint8_t		padding[5];
		};
		onMemAlloc( me, &a );
	}

	srand( 123456 );
	for ( size_t i = 0; i < 4096; i++ ) {
		const uint64_t addr =	( ( uint64_t )rand() |
								  ( uint64_t )rand() << 16 |
								  ( uint64_t )rand() << 32 ) + 7 & ~7;
		const uint32_t size = ( uint64_t )rand() & ( maxAllocSize - 1 );

		struct allocInfo_type a = {
			0,			// time
			rand() % ( sizeof( h ) / sizeof( h[ 0 ] ) ), // uint64_t	heapID;
			addr,		//uint64_t	systemAddress;
			addr + 8,	//uint64_t	userAddress;
			0,			//uint64_t*	callstack;
			size,		//uint32_t	requestedSize;
			size + ( rand() & ( maxAllocOverhead - 1 ) ), //uint32_t	actualSize;
			0,			//uint32_t	line;
			( uint16_t )-1,			//uint16_t	file;
			0,			//uint16_t	tag;
			0,			//uint16_t	align;
			0			//uint8_t		callstackDepth;
						//uint8_t		padding[5];
		};
		if ( ( i & 3 ) != 0 ) {
			onMemFree( me, &a );
		}
	}
#endif // 0

	return &me->pluginInterface;
}
