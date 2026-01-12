/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "precompiled.h"

#include "memory.h"
#include "memory_ui.h"
#include "plugin.h"
#include "symbol.h"

#pragma warning( push, 0 )
#include "../gui/imgui.h"
#pragma warning( pop )

#define PLUGIN_MEMORYUI_CHECKSUM 0x4d454d4f52595549 /* 'MEMORYUI' */

#define TEXT_SIZE 64

enum display_enum {
	AS_RAW,
	AS_HEX,
	AS_GILOBITE,
	AS_COMMIFY,
};

enum stat_flag {
	STATFLAG_RENDER_UI,
	STATFLAG_DIRTY,
};

typedef enum _column_t {
	COLUMN_TIME,
	COLUMN_REQUESTED,
	COLUMN_ACTUAL,
	COLUMN_WASTE,
	COLUMN_REQUESTED_HIWATER,
	COLUMN_ACTUAL_HIWATER,
	COLUMN_WASTE_HIWATER,
	COLUMN_ACTIVE_COUNT,
	COLUMN_ALLOC_COUNT,

	COLUMN_MAX
} column_t;

static struct {
	const char * const name;
	const display_enum as;
	uint32_t padding;
} MEMORY_STAT[] = {
	{ "Time", AS_HEX },
	{ "Requested", AS_GILOBITE },
	{ "Actual", AS_GILOBITE },
	{ "Waste", AS_GILOBITE },
	{ "Requested High Water", AS_GILOBITE },
	{ "Actual High Water", AS_GILOBITE },
	{ "Waste High Water", AS_GILOBITE },
	{ "Active Count", AS_COMMIFY },
	{ "Alloc Count", AS_COMMIFY },
};

typedef struct _statInfo_t {
	struct _memoryUIData_t*	owner;
	struct _statInfo_t*		parent;
	uint64_t				uid;
	const char *			name; /* serves as a friendly name for heap/function name for allocation */
	const char *			file;
	uint32_t				line;
	uint32_t				flags; /* 1<<STATFLAG_* */
	uint64_t				value[ COLUMN_MAX ];
	char					string[ COLUMN_MAX ][ 32 ];
	struct _statInfo_t **	children;
	uint64_t *				childrenUID;
	size_t					childrenCount;
} statInfo_t;

typedef struct _memoryUIData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct systemInterface_type *	systemInterface;
	symbolInterface_t*				symbolInterface;
	statInfo_t**					heap;
	size_t							heapCount;
	float							dataColumnWidths[ sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ) ];
	uint32_t						padding;
} memoryUIData_t;

static struct {
	union {
		statInfo_t statInfo;
	} u;
} placebo;

/*
========================
pluginInterfaceToMe
========================
*/
static memoryUIData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_MEMORYUI_CHECKSUM ) {
			return ( memoryUIData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
commify
========================
*/
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
				if ( tmp > 1024.0 ) {
					tmp /= 1024.0;
					*ext = "T";
				}
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
static statInfo_t* findOrAddHeap( memoryUIData_t * const me, const uint64_t heapID ) {
	size_t i;
	statInfo_t ** list;
	statInfo_t * heap;

	for ( i = 0; i < me->heapCount; ++i ) {
		if ( me->heap[ i ] && me->heap[ i ]->uid == heapID ) {
			return me->heap[ i ];
		}
	}

	list = ( statInfo_t** )me->systemInterface->allocate( me->systemInterface, sizeof( statInfo_t* ) * ( me->heapCount + 1 ) );
	if ( list == NULL ) {
		return &placebo.u.statInfo;
	}

	me->heapCount++;

	if ( me->heap ) {
		memcpy( list, me->heap, sizeof( statInfo_t* ) * me->heapCount );
		me->systemInterface->deallocate( me->systemInterface, me->heap );
	}

	me->heap = list;

	heap = ( statInfo_t * )me->systemInterface->allocate( me->systemInterface, sizeof( statInfo_t ) );
	me->heap[ me->heapCount - 1 ] = heap;
	if ( heap == NULL ) {
		return &placebo.u.statInfo;
	}

	memset( heap, 0, sizeof( statInfo_t ) );
	heap->uid = heapID;
	heap->owner = me;

	return heap;
}

/*
========================
findOrAddChild
========================
*/
static statInfo_t* findOrAddChild( memoryUIData_t * const me, statInfo_t * const parent, const uint64_t id ) {
	statInfo_t * stat = NULL;

	if ( parent->children != NULL ) {
		size_t i;

		for ( i = 0; i < parent->childrenCount; ++i ) {
			if ( parent->childrenUID[ i ] == id ) {
				return parent->children[ i ];
			}
		}

		stat = NULL;
		for ( i = 0; i < parent->childrenCount; ++i ) {
			if ( parent->childrenUID[ i ] == 0 ) {
				stat = ( statInfo_t * )me->systemInterface->allocate( me->systemInterface, sizeof( statInfo_t ) );
				parent->children[ i ] = stat;
				parent->childrenUID[ i ] = id;
				if ( stat == NULL ) {
					return &placebo.u.statInfo;
				}
				break;
			}
		}
	}

	if ( stat == NULL ) {
		const size_t size = sizeof( statInfo_t** ) * ( parent->childrenCount + 1 );
		const size_t uidSize = sizeof( uint64_t ) * ( parent->childrenCount + 1 );
		statInfo_t ** const list = ( statInfo_t** )me->systemInterface->allocate( me->systemInterface, size );
		uint64_t * const uids = ( uint64_t* )me->systemInterface->allocate( me->systemInterface, uidSize );
		if ( list == NULL || uids == NULL ) {
			me->systemInterface->deallocate( me->systemInterface, list );
			me->systemInterface->deallocate( me->systemInterface, uids );
			return &placebo.u.statInfo;
		}

		if ( parent->children != NULL ) {
			memcpy( list, parent->children, sizeof( statInfo_t* ) * parent->childrenCount );
			memcpy( uids, parent->childrenUID, sizeof( uint64_t ) * parent->childrenCount );
			me->systemInterface->deallocate( me->systemInterface, parent->children );
			me->systemInterface->deallocate( me->systemInterface, parent->childrenUID );
		}

		parent->childrenCount++;
		parent->children = list;
		parent->childrenUID = uids;

		stat = ( statInfo_t * )me->systemInterface->allocate( me->systemInterface, sizeof( statInfo_t ) );
		parent->children[ parent->childrenCount - 1 ] = stat;
		parent->childrenUID[ parent->childrenCount - 1 ] = id;
		if ( stat == NULL ) {
			return &placebo.u.statInfo;
		}
	}

	memset( stat, 0, sizeof( statInfo_t ) );
	stat->uid = id;
	stat->owner = me;
	stat->parent = parent;

	return stat;
}

/*
========================
findChild
========================
*/
static statInfo_t * findChild( memoryUIData_t * const me, statInfo_t * const parent, const uint64_t id ) {
	size_t i;

	( void )me; /* placeholder */

	for ( i = 0; i < parent->childrenCount; ++i ) {
		if ( parent->childrenUID[ i ] == id ) {
			return parent->children[ i ];
		}
	}

	return NULL;
}

static void formatStat( statInfo_t * const stat ) {
	double num = 0;
	const char * ext = "";

	for ( size_t i = 1; i < sizeof( stat->value ) / sizeof( stat->value[ 0 ] ); i++ ) {
		switch ( MEMORY_STAT[ i ].as ) {
			case AS_RAW: {
				_snprintf_s( stat->string[ i ], sizeof( stat->string[ i ] ), _TRUNCATE, "%llu", stat->value[ i ] );
			} break;

			case AS_HEX: {
				_snprintf_s( stat->string[ i ], sizeof( stat->string[ i ] ), _TRUNCATE, "%llx", stat->value[ i ] );
			} break;

			case AS_GILOBITE: {
				gilobite( stat->value[ i ], &num, &ext );
				_snprintf_s( stat->string[ i ], sizeof( stat->string[ i ] ), _TRUNCATE, "%.2f%sB", num, ext );
			} break;

			case AS_COMMIFY: {
				char tmp[ 64 ];
				const char * const str = commify( tmp, sizeof( tmp ), stat->value[ i ] );
				_snprintf_s( stat->string[ i ], sizeof( stat->string[ i ] ), _TRUNCATE, "%s", str );
			}break;

			default: {
				assert( false );
			}
		}
		stat->string[ i ][ sizeof( stat->string[ i ] ) - 1 ] = 0;
	}
}

/*
========================
addStat
========================
*/
static void addStat( statInfo_t * const stat, const struct allocInfo_type * const info ) {
	stat->value[ COLUMN_TIME ] = max( stat->value[ COLUMN_TIME ], info->time );
	stat->value[ COLUMN_REQUESTED ] += info->requestedSize;
	stat->value[ COLUMN_ACTUAL ] += info->actualSize;
	stat->value[ COLUMN_WASTE ] = stat->value[ COLUMN_ACTUAL ] - stat->value[ COLUMN_REQUESTED ];
	stat->value[ COLUMN_REQUESTED_HIWATER ] = max( stat->value[ COLUMN_REQUESTED_HIWATER ], stat->value[ COLUMN_REQUESTED ] );
	stat->value[ COLUMN_ACTUAL_HIWATER ] = max( stat->value[ COLUMN_ACTUAL_HIWATER ], stat->value[ COLUMN_ACTUAL ] );
	stat->value[ COLUMN_WASTE_HIWATER ] = max( stat->value[ COLUMN_WASTE_HIWATER ], stat->value[ COLUMN_WASTE ] );
	++stat->value[ COLUMN_ACTIVE_COUNT ];
	++stat->value[ COLUMN_ALLOC_COUNT ];
	stat->flags |= 1 << STATFLAG_DIRTY;}

/*
========================
subStat
========================
*/
static void subStat( statInfo_t * const stat, const struct allocInfo_type * const info ) {
	stat->value[ COLUMN_TIME ] = max( stat->value[ COLUMN_TIME ], info->time );
	stat->value[ COLUMN_REQUESTED ] -= info->requestedSize;
	stat->value[ COLUMN_ACTUAL ] -= info->actualSize;
	stat->value[ COLUMN_WASTE ] = stat->value[ COLUMN_ACTUAL ] - stat->value[ COLUMN_REQUESTED ];
	--stat->value[ COLUMN_ACTIVE_COUNT ];
	stat->flags |= 1 << STATFLAG_DIRTY;
}

/*
========================
destroyStat_r
========================
*/
static void destroyStat_r( memoryUIData_t * const me, statInfo_t * const stat ) {
	if ( stat->children ) {
		size_t i;

		for ( i = 0; i < stat->childrenCount; ++i ) {
			destroyStat_r( me, stat->children[ i ] );
		}

		me->systemInterface->deallocate( me->systemInterface, stat->children );
		me->systemInterface->deallocate( me->systemInterface, stat->childrenUID );
	}
	me->systemInterface->deallocate( me->systemInterface, stat );
}

/*
========================
onHeapCreate
========================
*/
static void onHeapCreate( void * const param, const struct heapInfo_type * const info ) {
	memoryUIData_t * const me = ( memoryUIData_t * )param;
	statInfo_t * const heap = findOrAddHeap( me, info->heapID );
	heap->name = info->name;
}

/*
========================
onMemAlloc
========================
*/
static void onMemAlloc( void * const param, const struct allocInfo_type * const info ) {
	memoryUIData_t * const me = ( memoryUIData_t * )param;
	statInfo_t * const heap = findOrAddHeap( me, info->heapID );
	statInfo_t * child = heap;

	addStat( heap, info );

	for ( size_t i = info->callstackDepth; i != 0 ; i-- ) {
		child = findOrAddChild( me, child, info->callstack[ i - 1 ] );
		child->uid = info->callstack[ i - 1 ];
		addStat( child, info );
	}
}

/*
========================
onMemFree
========================
*/
static void onMemFree( void * const param, const struct allocInfo_type * const info ) {
	memoryUIData_t * const me = ( memoryUIData_t * )param;
	statInfo_t * const heap = findOrAddHeap( me, info->heapID );
	statInfo_t * child = heap;

	for ( size_t i = info->callstackDepth; i != 0 ; i-- ) {
		child = findChild( me, child, info->callstack[ i - 1 ] );
		if ( child == NULL ) {
			break;
		}
		subStat( child, info );
	}

	subStat( heap, info );
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
	me->symbolInterface = ( symbolInterface_t* )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_SYMBOL );

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

	/*	todo: typically we should clear,but we only stop if we're being destroyed (currently...)
		and this takes an excessive amount of time to clean up. */
	/*clear( me );*/
}

/*
========================
myUpdate
========================
*/
static void myUpdate( struct pluginInterface_type * const self ) {
	memoryUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}
}

static void renderStatChildren_r( memoryUIData_t * const me, statInfo_t * const parent, const int indent ) {
	for ( size_t i = 0; i < parent->childrenCount; i++ ) {
		statInfo_t * const stat = parent->children[ i ];
		if ( stat->flags & ( 1 << STATFLAG_DIRTY ) ) {
			formatStat( stat );
			stat->flags &= ~( 1 << STATFLAG_DIRTY );
		}
		const bool processChildren = ImGui::TreeNode( ( void * )stat->uid, "%llx", stat->uid );
		ImGui::NextColumn();

		for ( size_t j = 1; j < sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ); j++ ) {
			ImGui::TextUnformatted( stat->string[ j ], stat->string[ j ] + strlen( stat->string[ j ] ) );
			ImGui::NextColumn();
		}

		ImGui::Separator();

		if ( processChildren ) {
			renderStatChildren_r( me, stat, indent + 1 );
			ImGui::TreePop();
		}
	}
}

static void renderHeap( memoryUIData_t * const me, statInfo_t * const heap ) {
	char txt[ 256 ];
	_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%s##%llx", heap->name ? heap->name : "[unnamed]", heap->uid );
	txt[ sizeof( txt ) - 1 ] = 0;

	bool isOpen = true;
	if ( ImGui::Begin( txt, &isOpen, ImGuiWindowFlags_HorizontalScrollbar ) ) {
		double num = 0;
		const char * ext = "";
		gilobite( heap->value[ COLUMN_ACTUAL ], &num, &ext );

		ImGui::Text( "%s: %.3f%sB", heap->name ? heap->name : "[unnamed]", num, ext );

		static int padMul = 2;
		static int fontMul = 3;
		static int spaceMul = 3;
		ImVec2 size = ImGui::GetWindowSize();
		const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
		const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
		size.y -= ImGui::GetFontSize() * fontMul + framePadding.y * padMul + itemSpacing.y * spaceMul;
		size.x -= framePadding.x * padMul;

		const int wasCol = ImGui::GetColumnsCount();
		ImGui::Columns( sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ) );

		ImGui::Text( "%s", "Callstack" );
		ImGui::NextColumn();
		ImGui::SetColumnWidth( 0, me->dataColumnWidths[ 0 ] + framePadding.x );
		for ( size_t i = 1; i < sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ); i++ ) {
			ImGui::Text( "%s", MEMORY_STAT[ i ].name );
			ImGui::SetColumnWidth( i, me->dataColumnWidths[ i ] );
			ImGui::NextColumn();
		}
		ImGui::Columns( wasCol );

		ImGui::BeginChild( txt, size, true, ImGuiWindowFlags_HorizontalScrollbar );

		ImGui::Columns( sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ) );

		statInfo_t * parent = heap;
		while ( parent->childrenCount == 1 ) {
			 parent = parent->children[ 0 ];
		}

		renderStatChildren_r( me, parent, 0 );

		for ( size_t i = 0; i < sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ); i++ ) {
			me->dataColumnWidths[ i ] = ImGui::GetColumnWidth( i );
		}

		ImGui::EndChild();
		ImGui::Columns( wasCol );
	}

	if ( !isOpen ) {
		heap->flags &= ~( 1 << STATFLAG_RENDER_UI );
	}

	ImGui::End();
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

	const int wasCol = ImGui::GetColumnsCount();
	ImGui::Columns( sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ) );

	ImGui::Text( "Heap" );
	ImGui::NextColumn();

	for ( size_t i = 1; i < sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ); i++ ) {
		ImGui::Text( MEMORY_STAT[ i ].name );
		ImGui::NextColumn();
	}

	ImGui::Separator();

	uint64_t statTotal[ sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ) ] = {};
	for ( size_t i = 0; i < me->heapCount; i++ ) {
		const bool shouldRender = ImGui::IsRectVisible( ImVec2( 0, 0 ) );

		if ( shouldRender ) {
			statInfo_t * const heap = me->heap[ i ];
			const char * const name = heap->name ? heap->name : "[unnamed]";
			bool isChecked = !!( heap->flags & ( 1 << STATFLAG_RENDER_UI ) );
			if ( ImGui::Checkbox( name, &isChecked ) ) {
				if ( isChecked ) {
					heap->flags |= 1 << STATFLAG_RENDER_UI;
				} else {
					heap->flags &= ~( 1 << STATFLAG_RENDER_UI );
				}
			}

			ImGui::NextColumn();

			for ( size_t j = 1; j < sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ); j++ ) {
				statTotal[ j ] += heap->value[ j ];
				if ( heap->flags & ( 1 << STATFLAG_DIRTY ) ) {
					formatStat( heap );
					heap->flags &= ~( 1 << STATFLAG_DIRTY );
				}
				ImGui::TextUnformatted( heap->string[ j ], heap->string[ j ] + strlen( heap->string[ j ] ) );
				ImGui::NextColumn();
			}
		}
	}
	ImGui::Separator();
	ImGui::Separator();
	ImGui::Text( "Total" );
	ImGui::NextColumn();
	ImGui::NextColumn();

	for ( size_t j = 2; j < sizeof( MEMORY_STAT ) / sizeof( MEMORY_STAT[ 0 ] ); j++ ) {
		switch ( MEMORY_STAT[ j ].as ) {
			case AS_RAW: {
				ImGui::Text( "%llu", statTotal[ j ] );
			} break;

			case AS_HEX: {
				ImGui::Text( "%llx", statTotal[ j ] );
			} break;

			case AS_GILOBITE: {
				double num = 0;
				const char * ext = "";
				gilobite( statTotal[ j ], &num, &ext );
				ImGui::Text( "%.2f%sB", num, ext );
			} break;

			case AS_COMMIFY: {
				char tmp[ 64 ];
				const char * const str = commify( tmp, sizeof( tmp ), statTotal[ j ] );
				ImGui::Text( "%s", str );
			}break;

			default: {
				assert( false );
			}
		}
		ImGui::NextColumn();
	}

	ImGui::Columns( wasCol );

	for ( size_t i = 0; i < me->heapCount; i++ ) {
		statInfo_t * const heap = me->heap[ i ];
		if ( heap->flags & ( 1 << STATFLAG_RENDER_UI ) ) {
			renderHeap( me, heap );
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
	return "Memory Explorer";
}

/*
========================
MemoryUI_Create
========================
*/
struct pluginInterface_type * MemoryUI_Create( struct systemInterface_type * const sys ) {
	memoryUIData_t * const me = ( memoryUIData_t * )sys->allocate( sys, sizeof( memoryUIData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( memoryUIData_t ) );

	me->checksum = PLUGIN_MEMORYUI_CHECKSUM;

	me->pluginInterface.start	= myStart;
	me->pluginInterface.stop	= myStop;
	me->pluginInterface.update	= myUpdate;
	me->pluginInterface.render	= myRender;
	me->pluginInterface.getName	= myGetName;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
