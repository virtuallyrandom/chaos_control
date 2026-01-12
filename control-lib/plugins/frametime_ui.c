/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "frametime.h"
#include "platform.h"
#include "plugin.h"
#include "render.h"
#include "time.h"
#include "../image.h"

#define PLUGIN_FRAMETIME_UI_CHECKSUM 0x4652414d544d5549 /* 'FRAMTMUI' */

#define DUMP_DETAIL_FILE 0

#define ENTRIES_PER_BLOCK	4096
#define MAX_MAP_NAME_LENGTH	96
#define GOAL_FRAME_MICROSECONDS 17000
#define IMAGE_WIDTH 1536
#define IMAGE_HEIGHT 1080

static const char* PLATFORM_NAME[] = {
	"x64",
	"Durango",
	"Orbis",
	"Linux",
	"Yeti"
};
static const size_t PLATFORM_NAME_COUNT = sizeof( PLATFORM_NAME ) / sizeof( *PLATFORM_NAME );

static const char* BINARY_NAME[] = {
	"Debug",
	"Release",
	"Retail",
	"Unknown"
};
static const size_t BINARY_NAME_COUNT = sizeof( BINARY_NAME ) / sizeof( *BINARY_NAME );

static const char* RENDER_NAME[] = {
	"OpenGL",
	"d3d",
	"Vulkan",
	"Unknown"
};
static const size_t RENDER_NAME_COUNT = sizeof( RENDER_NAME ) / sizeof( *RENDER_NAME );

static const char* PRODUCTION_NAME[] = {
	"Development",
	"Building",
	"Production",
	"Loaded",
	"Unknown",
};
static const size_t PRODUCTION_NAME_COUNT = sizeof( PRODUCTION_NAME ) / sizeof( *PRODUCTION_NAME );

typedef enum _graphType_t {
	GT_STACKED_BAR,
	GT_SCATTER_PLOT,
	GT_GATHER,
	GT_SORTED_BAR,
} graphType_t;

#define GO_SUMMARY	1
#define GO_PIE		2
#define GO_GRAPH	4
#define GO_TITLE	8

typedef struct _fttInfo_t {
	const char *name;
	uint64_t maxPassTimeUS;
	uint64_t minFailTimeUS;
} fttInfo_t;

typedef frameTime_t* ( *getCurrentDataCB_t )( void );

/*
	HACK HACK HACK

	This REALLY needs to move to a player info plugin, not be processed here!

	HACK HACK HACK
*/
#define REMO_SYSTEM_PLAYER 4
#define REMO_PACKET_PLAYER_POSITION 1
#define MAX_PLAYERS 12
typedef struct _playerPacket_t {
	int8_t  player;
	uint8_t angle[ 3 ];
	float   position[ 3 ];
} playerPacket_t;

typedef struct _playerData_t {
	int8_t			playerCount;
	int8_t			padding[ 7 ];
	playerPacket_t	player[ MAX_PLAYERS ];
} playerData_t;

typedef struct _block_t {
	uint32_t			count;
	uint32_t			padding;
	struct _block_t*	next;
	uint8_t*			data;
} block_t;

typedef struct _stubData_t {
	union {
		playerData_t	player;
		frameTime_t		frameTime;
	} u;
} stubData_t;

typedef struct _frametimeUIData_t {
	uint64_t							checksum;
	struct pluginInterface_type			pluginInterface;
	struct systemInterface_type *		systemInterface;
	struct frametimeInterface_type *	frametimeInterface;
	struct renderInterface_type *		renderInterface;
	struct timeInterface_type *			timeInterface;
	struct platformInterface_type *		platformInterface;
	block_t *							playerBlockHead;
	block_t *							playerBlockTail;
	block_t *							pulseBlockHead;
	block_t *							pulseBlockTail;
	block_t *							mainBlockHead;
	block_t *							mainBlockTail;
	block_t *							renderBlockHead;
	block_t *							renderBlockTail;
	block_t *							gpuBlockHead;
	block_t *							gpuBlockTail;
	block_t *							resolutionScalingBlockHead;
	block_t *							resolutionScalingBlockTail;
	struct platformInfo_type			platformInfo;
	struct buildInfo_type				buildInfo;
	struct graphicsInfo_type			graphicsInfo;
	struct processorInfo_type			processorInfo;
	char								currentMapName[ MAX_MAP_NAME_LENGTH ];
	uint64_t							frameCount;
	uint64_t							mapStartTime;
	uint64_t							mapLastTime;
	uint64_t							pulseAtFPS[ MAX_FRAME_TIME_ENTRIES ];
	uint64_t							pulseTotalTime[ MAX_FRAME_TIME_ENTRIES ];
	uint64_t							pulseWorstTime[ MAX_FRAME_TIME_ENTRIES ];
	uint64_t							mainTotalTime[ MAX_FRAME_TIME_ENTRIES ];
	uint64_t							mainWorstTime[ MAX_FRAME_TIME_ENTRIES ];
	HWND								wnd;
	HFONT								fontSmall;
	int32_t								fontSmallW;
	int32_t								fontSmallH;
	HFONT								fontNormal;
	int32_t								fontNormalW;
	int32_t								fontNormalH;
	HFONT								fontLarge;
	int32_t								fontLargeW;
	int32_t								fontLargeH;
	int32_t								dimensionX;
	int32_t								dimensionY;
} frametimeUIData_t;

static const timeInfo_t resolutionScalingTimeInfo[] = {
	{	"X", 0, 160, 0, 160, 0, 0, UINT32_MAX	},
	{	"Y", 1, 0, 0, 255, 0, 0, UINT32_MAX	},
};

/*
	this is a bit weird, but this is the stub we return if an allocation fails.  it reduces the
	amount of error checking needed significantly, simplifying logic and reducing branches on
	an op that should never fail anyway.  if it does fail, it simply writes into this struct,
	which is never used, so the data is irrelevant.
*/
static stubData_t stubData;

/*
========================
pluginInterfaceToMe
========================
*/
static frametimeUIData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_FRAMETIME_UI_CHECKSUM ) {
			return ( frametimeUIData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
allocBlock
========================
*/
static block_t* allocBlock( frametimeUIData_t * const me, block_t * const parent, const size_t containerSize ) {
	const size_t size = sizeof( block_t ) + containerSize * ENTRIES_PER_BLOCK;
	block_t * const block = ( block_t* )me->systemInterface->allocate( me->systemInterface, size );
	if ( block == 0 ) {
		return 0;
	}

	if ( parent ) {
		parent->next = block;
	}

	block->count	= 0;
	block->next		= 0;
	block->data		= ( uint8_t* )( block + 1 );

	memset( block->data, 0, containerSize * ENTRIES_PER_BLOCK );

	return block;
}

/*
========================
freeBlock
========================
*/
static void freeBlock( frametimeUIData_t * const me, block_t * const block ) {
	block_t *ptr = block;
	while ( ptr != 0 ) {
		block_t * const tmp = ptr;
		ptr = ptr->next;
		me->systemInterface->deallocate( me->systemInterface, tmp );
	}
}

/*
========================
getBlock
========================
*/
static void* getBlock(	frametimeUIData_t * const me,
						block_t ** const head,
						block_t ** const tail,
						const size_t containerSize ) {
	if ( *tail == 0 ) {
		*tail = allocBlock( me, *tail, containerSize );
		*head = *tail;
		if ( *tail == 0 ) {
			return 0;
		}
	}

	if ( ( *tail )->count == ENTRIES_PER_BLOCK ) {
		*tail = allocBlock( me, *tail, containerSize );
		if ( *tail == 0 ) {
			return 0;
		}
	}

	return *tail;
}

/*
========================
getCurrentPlayerData
========================
*/
static playerData_t* getCurrentPlayerData( frametimeUIData_t * const me ) {
	block_t * const block = getBlock(	me,
										&me->playerBlockHead,
										&me->playerBlockTail,
										sizeof( playerData_t ) );

	if ( block == 0 ) {
		return &stubData.u.player;
	}

	return ( playerData_t* )block->data + block->count;
}

/*
========================
getCurrentPulseData
========================
*/
static frameTime_t* getCurrentPulseData( frametimeUIData_t * const me ) {
	block_t * const block = getBlock(	me,
										&me->pulseBlockHead,
										&me->pulseBlockTail,
										sizeof( frameTime_t ) );

	if ( block == 0 ) {
		return &stubData.u.frameTime;
	}

	return ( frameTime_t* )block->data + block->count;
}

/*
========================
getCurrentMainData
========================
*/
static frameTime_t* getCurrentMainData( frametimeUIData_t * const me ) {
	block_t * const block = getBlock(	me,
										&me->mainBlockHead,
										&me->mainBlockTail,
										sizeof( frameTime_t ) );

	if ( block == 0 ) {
		return &stubData.u.frameTime;
	}

	return ( frameTime_t* )block->data + block->count;
}

/*
========================
getCurrentRenderData
========================
*/
static frameTime_t* getCurrentRenderData( frametimeUIData_t * const me ) {
	block_t * const block = getBlock(	me,
										&me->renderBlockHead,
										&me->renderBlockTail,
										sizeof( frameTime_t ) );

	if ( block == 0 ) {
		return &stubData.u.frameTime;
	}

	return ( frameTime_t* )block->data + block->count;
}

/*
========================
getCurrentGPUData
========================
*/
static frameTime_t* getCurrentGPUData( frametimeUIData_t * const me ) {
	block_t * const block = getBlock(	me,
										&me->gpuBlockHead,
										&me->gpuBlockTail,
										sizeof( frameTime_t ) );

	if ( block == 0 ) {
		return &stubData.u.frameTime;
	}

	return ( frameTime_t* )block->data + block->count;
}

/*
========================
getCurrentReesolutionScalingData
========================
*/
static frameTime_t* getCurrentReesolutionScalingData( frametimeUIData_t * const me ) {
	block_t * const block = getBlock(	me,
										&me->resolutionScalingBlockHead,
										&me->resolutionScalingBlockTail,
										sizeof( frameTime_t ) );

	if ( block == 0 ) {
		return &stubData.u.frameTime;
	}

	return ( frameTime_t* )block->data + block->count;
}

/*
========================
reset
========================
*/
static void reset( frametimeUIData_t * const me ) {
	me->currentMapName[ 0 ] = 0;

	freeBlock( me, me->playerBlockHead );
	me->playerBlockHead = 0;
	me->playerBlockTail = 0;

	freeBlock( me, me->pulseBlockHead );
	me->pulseBlockHead = 0;
	me->pulseBlockTail = 0;

	freeBlock( me, me->mainBlockHead );
	me->mainBlockHead = 0;
	me->mainBlockTail = 0;

	freeBlock( me, me->renderBlockHead );
	me->renderBlockHead = 0;
	me->renderBlockTail = 0;

	freeBlock( me, me->gpuBlockHead );
	me->gpuBlockHead = 0;
	me->gpuBlockTail = 0;

	freeBlock( me, me->resolutionScalingBlockHead );
	me->resolutionScalingBlockHead = 0;
	me->resolutionScalingBlockTail = 0;

	me->currentMapName[ 0 ] = 0;

	me->frameCount = 0;
	me->mapStartTime = ( uint64_t )-1;
	me->mapLastTime = 0;

	memset( me->pulseAtFPS, 0, sizeof( me->pulseAtFPS ) );
	memset( me->pulseTotalTime, 0, sizeof( me->pulseTotalTime ) );
	memset( me->pulseWorstTime, 0, sizeof( me->pulseWorstTime ) );
	memset( me->mainTotalTime, 0, sizeof( me->mainTotalTime ) );
	memset( me->mainWorstTime, 0, sizeof( me->mainWorstTime ) );
}

/*
========================
RenderTime
========================
*/
static void RenderTime(	frametimeUIData_t * const me,
						image_t const img,
						const char * const displayName,
						const graphType_t type,
						const int32_t options,
						const rect_t rect,
						const int32_t padx,
						const int32_t pady,
						const block_t * const head,
						const timeInfo_t * const timeInfoList,
						const uint32_t rangeMax,
						const float rangeMaxDivisor,
						const char * const rangeAnnotation ) {
	typedef struct _frameTimeSortEntry {
		uint8_t	id;
		uint8_t padding[ 7 ];
		size_t	value;
	} frameTimeSortEntry_t;

	const double pieChartStep = 2 * 65536.0;
	const int32_t borderPadding = 2;

	const int32_t axisx = 70;
	const int32_t borderx = 2;

	const int32_t miny = rect.top + pady;
	const int32_t maxy = rect.bottom - pady;

	const int32_t minx = rect.left + padx + axisx;
	const int32_t maxx = rect.right - padx;

	const int32_t rngy = maxy - miny;

	frameTimeSortEntry_t timeSort[ MAX_FRAME_TIME_ENTRIES ];
	uint64_t smoothed[ MAX_FRAME_TIME_ENTRIES ];
	uint32_t maxPlot[ MAX_FRAME_TIME_ENTRIES ];
	frameTime_t total;
	frameTime_t tolerance;
	uint32_t i, j;

	const block_t *block;
	double sum = 0;
	double pct;
	uint64_t numPlots = 0;
	rect_t tr;
		
	const float dist = (rngy / 2.0f ) - borderx;
	const int centerx = ( int )( minx + dist * 3 - borderx * 2 );
	const int centery = ( int )( maxy - ( maxy - miny ) / 2 );

	float ix = 0.0f;

#if DUMP_DETAIL_FILE
	{
		char filename[ 260 ];
		FILE *detailFile;
		uint64_t frame = 0;
		uint32_t columnCount = 0;

		_snprintf_s( filename, sizeof( filename ), _TRUNCATE, "%s.txt", displayName );
		filename[ sizeof( filename ) - 1 ] = 0;
		detailFile = fopen( filename, "w" );

		/* count columns */
		block = head;
		while ( block != 0 ) {
			const frameTime_t * const data = ( const frameTime_t* )block->data;

			for ( i = 0; i < block->count; ++i ) {
				const frameTime_t * const packet = data + i;
				columnCount = max( columnCount, packet->count );
			}
			block = block->next;
		}

		/* column header: frame */
		fprintf( detailFile, "Frame\t" );
		for ( i = 0; i < columnCount; ++i ) {
			fprintf( detailFile, "%s\t", timeInfoList[ i ].name );
		}
		fprintf( detailFile, "\n" );

		block = head;
		while ( block != 0 ) {
			const frameTime_t * const data = ( const frameTime_t* )block->data;

			for ( i = 0; i < block->count; ++i ) {
				const frameTime_t * const packet = data + i;
				const uint32_t count = packet->count;
				const uint32_t * const src = packet->entry;
				fprintf( detailFile, "%" PRIu64 "\t", frame++ );
				for ( j = 0; j < count; ++j ) {
					fprintf( detailFile, "%u\t", src[ j ] );
				}
				fprintf( detailFile, "\n" );
			}

			block = block->next;
		}

		fclose( detailFile );
	}
#endif /* DUMP_DETAIL_FILE */

	block = head;
	while ( block != 0 ) {
		numPlots += block->count;
		block = block->next;
	}
	numPlots -= 1;

	pct = 1.0f - ( ( numPlots - 1.0f ) / numPlots );

	/* sum the data while also calculating the maximum index and tolerances */
	memset( &total, 0, sizeof( total ) );
	memset( &smoothed, 0, sizeof( smoothed ) );
	memset( &tolerance, 0, sizeof( tolerance ) );
	memset( maxPlot, 0, sizeof( maxPlot ) );
	block = head;
	while ( block != 0 ) {
		const frameTime_t * const data = ( const frameTime_t* )block->data;
		for ( i = 0; i < block->count; ++i ) {
			const frameTime_t * const packet = data + i;
			const uint32_t count = packet->count;
			const uint32_t * const src = packet->entry;
			uint32_t * const dst = total.entry;
			total.count = max( total.count, count );
			for ( j = 0; j < count; ++j ) {
				maxPlot[ j ] = max( maxPlot[ j ], src[ j ] );
				dst[ j ] += src[ j ];
				sum += src[ j ];
				tolerance.entry[ j ] += src[ j ] <= timeInfoList[ j ].minFailTimeUS;
				smoothed[ j ] += ( uint32_t )( ( src[ j ] * 1000 ) * pct );
			}
		}
		block = block->next;
	}

	/* subtract the maximal plot from the smoothed data */
	for ( i = 0; i < total.count; ++i ) {
		smoothed[ i ] -= ( uint32_t )( ( maxPlot[ i ] * 1000 ) * pct );
		total.entry[ i ] -= maxPlot[ i ];
		sum -= maxPlot[ i ];
	}

	/* draw the pie chart */
	if ( ( options & GO_PIE ) == GO_PIE ) {
		float end = 0.0f;
		for ( i = 0; i < total.count; ++i ) {
			timeSort[ i ].id = ( uint8_t )i;
			timeSort[ i ].value = total.entry[ i ];
		}
		for ( i = 0; i < total.count; ++i ) {
			for ( uint32_t k = i + 1; k < total.count; ++k ) {
				if ( timeSort[ k ].value > timeSort[ i ].value ) {
					const frameTimeSortEntry_t e =  timeSort[ i ];
					timeSort[ i ] = timeSort[ k ];
					timeSort[ k ] = e;
				}
			}
		}
		ix = 0.0f;
		for ( j = 0; j < total.count; ++j ) {
			end += ( float )( total.entry[ timeSort[ j ].id ] / sum * pieChartStep );
			while ( ix < end ) {
				const uint32_t	color =	( ( uint32_t )timeInfoList[ timeSort[ j ].id ].red   <<  0 ) |
										( ( uint32_t )timeInfoList[ timeSort[ j ].id ].green <<  8 ) |
										( ( uint32_t )timeInfoList[ timeSort[ j ].id ].blue  << 16 );
				const double	val = 2 * M_PI * ix / pieChartStep;
				const int32_t	dx = ( int )( dist * cos( val ) );
				const int32_t	dy = ( int )( dist * sin( val ) );
				ImageLineStart( img, centerx, centery, 1, color );
				ImageLineMove( img, centerx + dx, centery - dy );
				ix += 1.0f;
			}
		}
	}

	tr = rect;

	tr.left += ( int )( dist * 4 + borderx );
	if ( ( options & GO_GRAPH ) == GO_GRAPH ) {
		const int32_t miny = tr.top + pady;
		const int32_t maxy = tr.bottom - pady;

		const int32_t minx = tr.left + padx + axisx;
		const int32_t maxx = tr.right - padx;

		const uint32_t rngy = maxy - miny;
		const uint32_t rngx = maxx - minx;

		float barstep = rngy / 4.0f;
		float bary = ( float )miny;

		const float xStepPer = ( rngx - 1.0f ) / numPlots;

		char str[ 64 ];

		if ( type == GT_SORTED_BAR ) {
			typedef struct _sort_t {
				uint32_t count;
				uint32_t id[ MAX_FRAME_TIME_ENTRIES ];
				float value[ MAX_FRAME_TIME_ENTRIES ];
			} sort_t;
			sort_t *val = ( sort_t* )me->systemInterface->allocate( me->systemInterface, sizeof( sort_t ) * ( rngx + 1 ) );

			uint32_t i, j, k;
			float ix;

			memset( val, 0, sizeof( sort_t ) * rngx );
		
			/* gather all of the data for each column, unsorted */
			ix = 0;
			block = head;
			while ( block != 0 ) {
				const frameTime_t * const data = ( const frameTime_t* )block->data;
				for ( i = 0; i < block->count; ++i ) {
					const frameTime_t * const pulse = data + i;
					const int nx = ( int )ix;
					val[ nx ].count = pulse->count;
					for ( j = 0; j < pulse->count; ++j ) {
						val[ nx ].value[ j ] = max( val[ nx ].value[ j ], pulse->entry[ j ] );
						val[ nx ].id[ j ] = j;
					}
					ix += xStepPer;
				}
				block = block->next;
			}

			/* sort each column, highest to lowest */
			ix = 0;
			for ( i = 0; i < rngx; ++i ) {
				sort_t * const sort = val + i;
				for ( j = 0; j < sort->count; ++j ) {
					for ( k = j + 1; k < sort->count; ++k ) {
						if ( sort->value[ j ] < sort->value[ k ] ) {
							const float tf = sort->value[ j ];
							const uint32_t te = sort->id[ j ];
							sort->value[ j ] = sort->value[ k ];
							sort->value[ k ] = tf;
							sort->id[ j ] = sort->id[ k ];
							sort->id[ k ] = te;
						}
					}
				}
			}

			/* draw */
			for ( i = 0; i < rngx; ++i ) {
				for ( j = 0; j < val[ i ].count; ++j ) {
					const sort_t * const entry = val + i;
					const uint32_t	color =	( ( uint32_t )timeInfoList[ entry->id[ j ] ].red   <<  0 ) |
											( ( uint32_t )timeInfoList[ entry->id[ j ] ].green <<  8 ) |
											( ( uint32_t )timeInfoList[ entry->id[ j ] ].blue  << 16 );
					const float gy = min( entry->value[ j ], rangeMax ) / ( float )rangeMax * rngy;
					ImageLineStart( img, minx + i, maxy, 1, color );
					ImageLineMove( img, minx + i, maxy - ( int )gy );
				}
			}

			me->systemInterface->deallocate( me->systemInterface, val );
		} else if ( type == GT_STACKED_BAR ) {
			typedef struct _sort_t {
				uint64_t	sum;
				uint32_t	padding;
				frameTime_t	time;
			} sort_t;
			sort_t * const val = ( sort_t* )me->systemInterface->allocate( me->systemInterface, sizeof( sort_t ) * ( rngx + 1 ));

			if ( val != 0 ) {
				float ix = 0;
				uint32_t maxCount = 0;

				memset( val, 0, sizeof( sort_t ) * rngx );
		
				/* gather all of the data for each column, unsorted */
				block = head;
				while ( block != 0 ) {
					const frameTime_t * const data = ( const frameTime_t* )block->data;
					for ( i = 0; i < block->count; ++i ) {
						const frameTime_t * const packet = data + i;
						uint64_t sum = 0;
						const int nx = ( int )ix;
						maxCount = max( maxCount, packet->count );
						for ( j = 0; j < packet->count; ++j ) {
							sum += packet->entry[ j ];
						}
						if ( sum > val[ nx ].sum ) {
							val[ nx ].sum = sum;
							val[ nx ].time = *packet;
						}
						ix += xStepPer;
					}
					block = block->next;
				}

				/* draw a stacked bar of all of the values */
				for ( i = 0; i < rngx; ++i ) {
					int starty = maxy;
					for ( j = 0; j < maxCount; ++j ) {
						const uint32_t	color =	( ( uint32_t )timeInfoList[ j ].red   <<  0 ) |
												( ( uint32_t )timeInfoList[ j ].green <<  8 ) |
												( ( uint32_t )timeInfoList[ j ].blue  << 16 );
						const int endy = max( miny, starty - ( int32_t )( val[ i ].time.entry[ j ] / ( float )rangeMax * rngy ) );
						ImageLineStart( img, minx + i, starty, 1, color );
						ImageLineMove( img, minx + i, endy );
						starty = endy;
					}
				}

				me->systemInterface->deallocate( me->systemInterface, val );
			}
		} else if ( type == GT_SCATTER_PLOT ) {
			ix = 0;
			block = head;
			while ( block ) {
				const frameTime_t * const data = ( const frameTime_t* )block->data;
				for ( i = 0; i < block->count; ++i ) {
					const frameTime_t * const packet = data + i;
					for ( j = 0; j < packet->count; ++j ) {
						const uint32_t	color =	( ( uint32_t )timeInfoList[ j ].red   <<  0 ) |
												( ( uint32_t )timeInfoList[ j ].green <<  8 ) |
												( ( uint32_t )timeInfoList[ j ].blue  << 16 );
						const int dy = max( miny, maxy - ( int32_t )( packet->entry[ j ] / ( float )rangeMax * rngy ) );
						ImagePlot( img, minx + ( int )ix, ( int )dy, color );
					}
					ix += xStepPer;
				}
				block = block->next;
			}
		}

		ImageLineStart( img, minx, miny, 1, 0xdddddd );
		ImageLineMove( img, minx, maxy );
		ImageLineMove( img, minx, miny );
		ImageLineMove( img, maxx, miny );
		ImageLineMove( img, minx, miny );

		/* draw the bars after so they're always on top */
		tr.left = minx + borderx;
		tr.right = maxx - borderx;

		bary += barstep;
		tr.top = ( int )bary - me->fontNormalH;
		( void )snprintf( str, sizeof( str ), "%.2f%s", ( rangeMax / rangeMaxDivisor ) * 0.75f, rangeAnnotation );
		str[ sizeof( str ) - 1 ] = 0;
		ImageText( img, &tr, str/*"50.000ms"*/, 0, TE_LEFT, me->fontNormal );
		ImageLineMove( img, minx, ( int )( bary ) );
		ImageLineMove( img, maxx, ( int )( bary ) );
		ImageLineMove( img, minx, ( int )( bary ) );

		bary += barstep;
		tr.top = ( int )bary - me->fontNormalH;
		( void )snprintf( str, sizeof( str ), "%.2f%s", ( rangeMax / rangeMaxDivisor ) * 0.5f, rangeAnnotation );
		str[ sizeof( str ) - 1 ] = 0;
		ImageText( img, &tr, str/*"33.333ms"*/, 0, TE_LEFT, me->fontNormal );
		ImageLineMove( img, minx, ( int )( bary ) );
		ImageLineMove( img, maxx, ( int )( bary ) );
		ImageLineMove( img, minx, ( int )( bary ) );

		bary += barstep;
		tr.top = ( int )bary - me->fontNormalH;
		( void )snprintf( str, sizeof( str ), "%.2f%s", ( rangeMax / rangeMaxDivisor ) * 0.25f, rangeAnnotation );
		str[ sizeof( str ) - 1 ] = 0;
		ImageText( img, &tr, str/*"16.667ms"*/, 0, TE_LEFT, me->fontNormal );
		ImageLineMove( img, minx, ( int )( bary ) );
		ImageLineMove( img, maxx, ( int )( bary ) );
		ImageLineMove( img, minx, ( int )( bary ) );
	}

	ImageLineStart( img, rect.left, rect.top, 2, 0 );
	ImageLineMove( img, rect.right - 1, rect.top - 1 );
	ImageLineMove( img, rect.right - 1, rect.bottom );
	ImageLineMove( img, rect.left, rect.bottom );
	ImageLineMove( img, rect.left, rect.top - 1 );

	tr = rect;
	tr.left += borderx;
	tr.right -= borderx * borderPadding * 2;
	tr.top += me->fontLargeH + borderx * borderPadding;
	tr.bottom -= borderx * 2;

	/* sort the times for displaying the text */
	for ( i = 0; i < total.count; ++i ) {
		timeSort[ i ].id = ( uint8_t )i;
		timeSort[ i ].value = total.entry[ i ];
	}
	for ( i = 0; i < total.count; ++i ) {
		uint32_t j;
		for ( j = i + 1; j < total.count; ++j ) {
			if ( timeSort[ j ].value > timeSort[ i ].value ) {
				const frameTimeSortEntry_t e =  timeSort[ i ];
				timeSort[ i ] = timeSort[ j ];
				timeSort[ j ] = e;
			}
		}
	}

	tr.left += borderx * borderPadding * 2;

	if ( ( options & GO_SUMMARY ) == GO_SUMMARY ) {
		/* draw the average times from each category */
		const int32_t hstep = 4 * me->fontNormalW;

		tr.right = maxx;

		for ( i = 0; i < total.count; ++i ) {
			const uint8_t id = timeSort[ i ].id;
			char txt[ 256 ];
			const uint32_t	color =	( ( uint32_t )timeInfoList[ id ].red   <<  0 ) |
									( ( uint32_t )timeInfoList[ id ].green <<  8 ) |
									( ( uint32_t )timeInfoList[ id ].blue  << 16 );
			const float avg = smoothed[ id ] / 1000.0f;
			const uint32_t avgColor = RGB( 0, 0, 0 );

			if ( tr.top >= rect.bottom - borderx * borderPadding * 2 ) {
				break;
			}

			ImageLineStart( img, tr.left, tr.top, 1, 0xbbbbbb );
			ImageLineMove( img, tr.left + ( int )( centerx - dist ), tr.top );

			tr.right = tr.left + hstep;

			_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%2.2f", avg / 1000.0f );
			ImageText( img, &tr, txt, avgColor, TE_RIGHT, me->fontNormal );

			tr.left += hstep + borderx * 2;
			tr.right = maxx - borderx;

			_snprintf_s( txt, sizeof( txt ), _TRUNCATE, "%s", timeInfoList[ id ].name );
			tr.top += 1;
			tr.left += 1;
			ImageText( img, &tr, txt, color / 2, TE_LEFT, me->fontNormal );
			tr.top -= 1;
			tr.left -= 1;
			ImageText( img, &tr, txt, color, TE_LEFT, me->fontNormal );

			tr.left -= hstep + borderx * 2;
			tr.top += me->fontNormalH + borderx;
		}
	}

	/* draw the display name */
	if ( ( options & GO_TITLE ) == GO_TITLE ) {
		tr = rect;
		tr.left += borderx;
		tr.top += borderx;
		ImageText( img, &tr, displayName, 0, TE_LEFT, me->fontLarge );
	}
}

/*
========================
writeBitmap
========================
*/
static void writeBitmap( frametimeUIData_t * const me ) {
	const float		IMAGE_MAX_MS = 16.6666666667f * 4;
	const float		IMAGE_MAX_S = IMAGE_MAX_MS / 1000;
	const uint32_t	IMAGE_MAX_US = ( uint32_t )( IMAGE_MAX_S * 1000000 );

	static const int32_t linex = 8;
	static const int32_t fonty = 16;
	static const int32_t fontx = 12;
	static const int32_t borderx = 2;

	char text[ 256 ];
	char *mapName;
	block_t *block;
	rect_t r;
	image_t img;
	int32_t i;
	int32_t n;
	int32_t gy;
	int32_t hh;
	int32_t mm;
	int32_t sc;
	uint64_t mainSum;
	uint64_t mainMax;
	uint64_t mainCount;
	float mainAvgFps;
	float mainAvgPct;

	size_t platformIndex = 4;
	size_t binaryIndex = 3;
	size_t renderIndex = 3;
	size_t productionIndex = 4;

	if ( me->frameCount == 0 ) {
		return;
	}

	if ( me->playerBlockTail == 0 ) {
		reset( me );
		return;
	}

	if ( me->currentMapName[ 0 ] == 0 ) {
		snprintf( me->currentMapName, sizeof( me->currentMapName ), "Unknown" );
		me->currentMapName[ sizeof( me->currentMapName ) - 1 ] = 0;
	}

	platformIndex	= min( PLATFORM_NAME_COUNT - 1, me->platformInfo.platform );
	binaryIndex		= min( BINARY_NAME_COUNT - 1, me->platformInfo.binaryType );
	renderIndex		= min( RENDER_NAME_COUNT - 1, me->platformInfo.renderType );
	productionIndex	= min( PRODUCTION_NAME_COUNT - 1, me->platformInfo.productionType );

	img = ImageCreate( IMAGE_WIDTH, IMAGE_HEIGHT );

	r.top		= borderx;
	r.left		= borderx;
	r.bottom	= r.top + fonty;
	r.right		= r.left + linex * fontx;

	mapName = me->currentMapName + strlen( me->currentMapName );
	while ( mapName > me->currentMapName ) {
		if ( mapName[ -1 ] == '\\' || mapName[ -1 ] == '/' ) {
			break;
		}
		--mapName;
	}

	hh = 0;
	mm = 0;
	sc = ( int32_t )( ( me->mapLastTime - me->mapStartTime ) / me->platformInfo.clockTicksPerSecond );
	if ( sc > 60 ) {
		mm = sc / 60;
		sc -= mm * 60;

		if ( mm > 60 ) {
			hh = mm / 60;
			mm -= hh * 60;
		}
	}

	/* calculate average time of Main Thread */
	/* HACK - Expects that MainThread is at index 0 of the frameTime_t */
	block = me->pulseBlockHead;
	mainCount = 0;
	mainSum = 0;
	mainMax = 0;
	while ( block ) {
		frameTime_t * const time = ( frameTime_t* )block->data;
		for ( i = 0; i < ( int32_t )block->count; ++i ) {
			const uint32_t value = time[ i ].entry[ 0 ];
			mainMax = max( mainMax, value );
			mainSum += value;
		}
		mainCount += block->count;
		block = block->next;
	}

	mainSum -= mainMax;
	mainCount--;

	/* left column */
	r.top = borderx;
	r.bottom = r.top + fonty;
	r.right = IMAGE_WIDTH - borderx;

	( void )snprintf(	text,
						sizeof( text ),
						"Map: %s",
						mapName );
	text[ sizeof( text ) - 1 ] = 0;
	ImageText( img, &r, text, 0, TE_LEFT, me->fontNormal );

	r.top += fonty;
	r.bottom += fonty;
	( void )snprintf(	text,
						sizeof( text ),
						"Duration: %d:%02d:%02d",
						hh,
						mm,
						sc );
	text[ sizeof( text ) - 1 ] = 0;
	ImageText( img, &r, text, 0, TE_LEFT, me->fontNormal );

	r.top += fonty;
	r.bottom += fonty;
	mainAvgFps = 1000.0f / ( mainSum / ( mainCount * 1000.0f ) );
	( void )snprintf(	text,
						sizeof( text ),
						"Avg FPS: %.2f",
						mainAvgFps );
	text[ sizeof( text ) - 1 ] = 0;
	mainAvgPct = min( 1.0f, ( 60.0f - max( 59.5f, mainAvgFps ) ) / ( 60.0f - 59.5f ) );
	ImageText(	img,
				&r,
				text,
				RGB( ( uint8_t )( mainAvgPct * 196 ), ( uint8_t )( ( 1.0f - mainAvgPct ) * 196 ), 0 ),
				TE_LEFT,
				me->fontNormal );

	/* mid column */
	r.top = borderx;
	r.bottom = r.top + fonty;
	r.left = ( IMAGE_WIDTH - borderx ) / 4;
	r.right = IMAGE_WIDTH - borderx;

	if ( me->processorInfo.name != 0 ) {
		( void )snprintf(	text,
							sizeof( text ),
							"Processor: %s",
							me->processorInfo.name );
		text[ sizeof( text ) - 1 ] = 0;
		ImageText( img, &r, text, 0, TE_LEFT, me->fontNormal );

		r.top += fonty;
		r.bottom += fonty;
	}

	if ( me->graphicsInfo.name && me->graphicsInfo.name[ 0 ] != 0 ) {
		( void )snprintf(	text,
							sizeof( text ),
							"Graphics: %s %s (ver. %s) @%dx%d",
							me->graphicsInfo.vendor ? me->graphicsInfo.vendor : "Unknown",
							me->graphicsInfo.name ? me->graphicsInfo.name : "Unknown",
							me->graphicsInfo.driverVersion ? me->graphicsInfo.driverVersion : "Unknown",
							me->dimensionX,
							me->dimensionY );
		text[ sizeof( text ) - 1 ] = 0;
		ImageText( img, &r, text, 0, TE_LEFT, me->fontNormal );

		r.top += fonty;
		r.bottom += fonty;
	}

	/* right column */
	r.top = borderx;
	r.bottom = r.top + fonty;
	r.left += fontx;

	( void )snprintf(	text,
						sizeof( text ),
						"Binary: %s, Package: %s",
						me->buildInfo.binaryDesc,
						me->buildInfo.packageDesc );
	text[ sizeof( text ) - 1 ] = 0;
	ImageText( img, &r, text, 0, TE_RIGHT, me->fontNormal );

	r.top += fonty;
	r.bottom += fonty;
	( void )snprintf(	text,
						sizeof( text ),
						"%04u.%02u.%02u %02u:%02u:%02u",
						me->platformInfo.timeInfo.year + 2000,
						me->platformInfo.timeInfo.month + 1,
						me->platformInfo.timeInfo.day,
						me->platformInfo.timeInfo.hour,
						me->platformInfo.timeInfo.minute,
						me->platformInfo.timeInfo.second );
	text[ sizeof( text ) - 1 ] = 0;
	ImageText( img, &r, text, 0, TE_RIGHT, me->fontNormal );

	r.top += fonty;
	r.bottom += fonty;
	( void )snprintf(	text,
						sizeof( text ),
						"Platform: %s, Binary: %s, Renderer: %s",
						PLATFORM_NAME[ platformIndex ],
						BINARY_NAME[ binaryIndex ],
						RENDER_NAME[ renderIndex ] );
	text[ sizeof( text ) - 1 ] = 0;
	ImageText( img, &r, text, 0, TE_RIGHT, me->fontNormal );

	r.top += fonty + borderx;
	r.bottom = IMAGE_HEIGHT - borderx;

	gy = ( r.bottom - r.top ) / 6;

	r.left = borderx;
	r.right = IMAGE_WIDTH - borderx;
	r.bottom = r.top + gy;
	RenderTime( me,
				img,
				"Overview",
				GT_SORTED_BAR,
				GO_SUMMARY | GO_PIE | GO_GRAPH | GO_TITLE,
				r,
				5,
				10,
				me->pulseBlockHead,
				me->frametimeInterface->getPulseTimeInfo( me->frametimeInterface, 0 ),
				IMAGE_MAX_US,
				1000.0f,
				"ms" );

	r.top += gy;
	r.bottom = r.top + gy;
	RenderTime( me,
				img,
				"Overview",
				GT_SCATTER_PLOT,
				GO_GRAPH,
				r,
				5,
				10,
				me->pulseBlockHead,
				me->frametimeInterface->getPulseTimeInfo( me->frametimeInterface, 0 ),
				IMAGE_MAX_US,
				1000.0f,
				"ms" );

	r.top += gy;
	r.bottom = r.top + gy;
	RenderTime(	me,
				img,
				"Main Thread Breakdown",
				GT_STACKED_BAR,
				GO_SUMMARY | GO_PIE | GO_GRAPH | GO_TITLE,
				r,
				5,
				10,
				me->mainBlockHead,
				me->frametimeInterface->getMainTimeInfo( me->frametimeInterface, 0 ),
				IMAGE_MAX_US,
				1000.0f,
				"ms" );

	r.top += gy;
	r.bottom = r.top + gy;
	RenderTime(	me,
				img,
				"Render Breakdown",
				GT_STACKED_BAR,
				GO_SUMMARY | GO_PIE | GO_GRAPH | GO_TITLE,
				r,
				5,
				10,
				me->renderBlockHead,
				me->frametimeInterface->getRenderTimeInfo( me->frametimeInterface, 0 ),
				IMAGE_MAX_US,
				1000.0f,
				"ms" );

	r.top += gy;
	r.bottom = r.top + gy;
	RenderTime(	me,
				img,
				"GPU Breakdown",
				GT_STACKED_BAR,
				GO_SUMMARY | GO_PIE | GO_GRAPH | GO_TITLE,
				r,
				5,
				10,
				me->gpuBlockHead,
				me->frametimeInterface->getRenderTimeInfo( me->frametimeInterface, 0 ),
				IMAGE_MAX_US,
				1000.0f,
				"ms" );

	r.top += gy;
	r.bottom = r.top + gy;
	RenderTime(	me,
				img,
				"Resolution Scaling",
				GT_SCATTER_PLOT,
				GO_GRAPH | GO_TITLE,
				r,
				5,
				10,
				me->resolutionScalingBlockHead,
				resolutionScalingTimeInfo,
				INT16_MAX,
				INT16_MAX / 100.0f,
				"%" );

	me->systemInterface->describeDataSource(	me->systemInterface,
												text,
												sizeof( text ),
												"%04u.%02u.%02u.%02u.%02u.%02u.%s.%s %s.%P.%s.%s.%s",
												me->platformInfo.timeInfo.year + 2000,
												me->platformInfo.timeInfo.month + 1,
												me->platformInfo.timeInfo.day,
												me->platformInfo.timeInfo.hour,
												me->platformInfo.timeInfo.minute,
												me->platformInfo.timeInfo.second,
												mapName,
												me->graphicsInfo.vendor ? me->graphicsInfo.vendor : "Unknown",
												me->graphicsInfo.name ? me->graphicsInfo.name : "Unknown",
												PLATFORM_NAME[ platformIndex ],
												BINARY_NAME[ binaryIndex ],
												RENDER_NAME[ renderIndex ],
												PRODUCTION_NAME[ productionIndex ] );

	/* replace anything that's not alpha, numeric, or '.' with '_' */
	n = 0;
	for ( i = 0; text[ i ]; ++i ) {
		const char cha = text[ i ] | 0x20;
		const char ch = text[ i ];
		if ( ch == ' ' ) {
			if ( text[ i + 1 ] == '.' ) {
				continue;
			}
			if ( i > 0 && text[ i - 1 ] == '.' ) {
				continue;
			}
		}

		if (	( cha < 'a' || cha > 'z' ) &&
				( ch < '0' || ch > '9' ) &&
				text[ i ] != '.' ) {
			text[ n++ ] = '_';
		} else {
			text[ n++ ] = text[ i ];
		}
	}
	text[ n ] = 0;

	ImageSave( img, text );

	ImageDestroy( img );

	reset( me );
}

/*
========================
onPulseData
========================
*/
static void onPulseData( frametimeUIData_t * const me, const frameTime_t * const data ) {
	playerData_t * const playerData = getCurrentPlayerData( me );
	frameTime_t * const pulseData = getCurrentPulseData( me );
	frameTime_t * const mainData = getCurrentMainData( me );
	frameTime_t * const renderData = getCurrentRenderData( me );
	frameTime_t * const gpuData = getCurrentGPUData( me );
	frameTime_t * const rsData = getCurrentReesolutionScalingData( me );
	uint32_t i;

	/* if no players were reported for the frame, simply reuse it for the next one */
	if ( playerData->playerCount == 0 ) {
		memset( playerData, 0, sizeof( playerData_t ) );
		memset( pulseData, 0, sizeof( frameTime_t ) );
		memset( mainData, 0, sizeof( frameTime_t ) );
		memset( renderData, 0, sizeof( frameTime_t ) );
		memset( gpuData, 0, sizeof( frameTime_t ) );
		memset( rsData, 0, sizeof( frameTime_t ) );
		return;
	}

	memcpy(	pulseData, data, sizeof( frameTime_t ) );

	/* update pulse time */
	for ( i = 0; i < data->count; ++i ) {
		me->pulseAtFPS[ i ] += pulseData->entry[ i ] <= GOAL_FRAME_MICROSECONDS ? 1 : 0;
		me->pulseTotalTime[ i ] += pulseData->entry[ i ];
		me->pulseWorstTime[ i ] = max( me->pulseWorstTime[ i ], pulseData->entry[ i ] );
	}

	/* update main time */
	for ( i = 0; i < mainData->count; ++i ) {
		me->mainTotalTime[ i ] += mainData->entry[ i ];
		me->mainWorstTime[ i ] = max( me->mainWorstTime[ i ], mainData->entry[ i ] );
	}

	/* pulses are the last thing on the frame, so they alone advance the blocks */
	me->playerBlockTail->count++;
	me->pulseBlockTail->count++;
	me->mainBlockTail->count++;
	me->renderBlockTail->count++;
	me->gpuBlockTail->count++;
	me->resolutionScalingBlockTail->count++;

	me->frameCount++;
}

/*
========================
onResolutionScaling
========================
*/
static void onResolutionScaling(	frametimeUIData_t * const me,
									const float x,
									const float y,
									const uint64_t time ) {
	frameTime_t * const dst = getCurrentReesolutionScalingData( me );

	( void )time;

	dst->count = 2;
	dst->entry[ 0 ] = ( uint32_t )( clamp( 0.0f, 1.0f, x ) * INT16_MAX );
	dst->entry[ 1 ] = ( uint32_t )( clamp( 0.0f, 1.0f, y ) * INT16_MAX );
}

/*
========================
onDimensions
========================
*/
static void onDimensions( frametimeUIData_t * const me, const int32_t x, const int32_t y ) {
	me->dimensionX = x;
	me->dimensionY = y;
}

/*
========================
onMainTimeData
========================
*/
static void onMainTimeData( frametimeUIData_t * const me, const frameTime_t * const data ) {
	frameTime_t * const dst = getCurrentMainData( me );
	const size_t nonArraySize = sizeof( frameTime_t ) - sizeof( data->entry );
	memcpy(	dst, data, nonArraySize + data->count * sizeof( data->entry[ 0 ] ) );
}


/*
========================
onRenderTimeData
========================
*/
static void onRenderTimeData( frametimeUIData_t * const me, const frameTime_t * const data ) {
	frameTime_t * const dst = getCurrentRenderData( me );
	const size_t nonArraySize = sizeof( frameTime_t ) - sizeof( data->entry );
	memcpy(	dst, data, nonArraySize + data->count * sizeof( data->entry[ 0 ] ) );
}

/*
========================
onGPUTimeData
========================
*/
static void onGPUTimeData( frametimeUIData_t * const me, const frameTime_t * const data ) {
	frameTime_t * const dst = getCurrentGPUData( me );
	const size_t nonArraySize = sizeof( frameTime_t ) - sizeof( data->entry );
	memcpy(	dst, data, nonArraySize + data->count * sizeof( data->entry[ 0 ] ) );
}

/*
========================
onPlatformInfo
========================
*/
static void onPlatformInfo( frametimeUIData_t * const me, const struct platformInfo_type * const data ) {
	memcpy( &me->platformInfo, data, sizeof( struct platformInfo_type ) );
}

/*
========================
onBuildInfo
========================
*/
static void onBuildInfo( frametimeUIData_t * const me, const struct buildInfo_type * const data ) {
	memcpy( &me->buildInfo, data, sizeof( struct buildInfo_type ) );
}

/*
========================
onGraphicsInfo
========================
*/
static void onGraphicsInfo( frametimeUIData_t * const me, const struct graphicsInfo_type * const data ) {
	memcpy( &me->graphicsInfo, data, sizeof( struct graphicsInfo_type ) );
}

/*
========================
onMapLoad
========================
*/
static void onMapLoad( frametimeUIData_t * const me, const char * const data ) {
	writeBitmap( me );
	strncpy( me->currentMapName, data, sizeof( me->currentMapName ) );
}

/*
========================
onProcessorInfo
========================
*/
static void onProcessorInfo( frametimeUIData_t * const me, const struct processorInfo_type * const data ) {
	memcpy( &me->processorInfo, data, sizeof( struct processorInfo_type ) );
}

/*
========================
onPlayerPosition
========================
*/
static void onPlayerPosition( frametimeUIData_t * const me, const struct packetHeader_type * const hdr ) {
	playerData_t * const playerData = getCurrentPlayerData( me );
	const playerPacket_t * const pkt = ( playerPacket_t* )hdr;
	int8_t playerIndex;

	const size_t headerSize = sizeof( struct packetHeader_type );
	const size_t playerPacketSize = sizeof( playerPacket_t );
	if ( hdr->size != headerSize + playerPacketSize ) {
		return;
	}

	me->mapStartTime		= min( hdr->time, me->mapStartTime );
	me->mapLastTime			= max( hdr->time, me->mapLastTime );
	playerIndex				= max( pkt->player, MAX_PLAYERS - 1 );
	playerData->playerCount	= max( playerData->playerCount, playerIndex + 1 );
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	LOGFONTA fnt;
	HDC dc;
	HBITMAP bmp;
	RECT r;

	frametimeUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->timeInterface		= ( struct timeInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_TIME );
	me->frametimeInterface	= ( struct frametimeInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_FRAMETIME );
	me->renderInterface		= ( struct renderInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_RENDER );
	me->platformInterface	= ( struct platformInterface_type * )me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_PLATFORM );

	me->wnd = me->systemInterface->createWindow( me->systemInterface, self, "Frame Time" );

	memset( &fnt, 0, sizeof( fnt ) );
	strcpy( fnt.lfFaceName, "Tahoma" );
	fnt.lfHeight = 13;
	me->fontSmall = CreateFontIndirectA( &fnt );

	fnt.lfHeight = 14;
	me->fontNormal = CreateFontIndirectA( &fnt );

	fnt.lfHeight = 16;
	fnt.lfWeight = 700;
	me->fontLarge = CreateFontIndirectA( &fnt );

	dc = CreateCompatibleDC( GetDC( 0 ) );
	bmp	= CreateCompatibleBitmap( GetDC( 0 ), 512, 512 );

	r.left = 0;
	r.top = 0;
	r.right = 512;
	r.bottom = 512;

	SelectObject( dc, bmp );

	SelectObject( dc, me->fontSmall );
	DrawTextA( dc, "A", 1, &r, DT_SINGLELINE|DT_CALCRECT );
	me->fontSmallH = r.bottom;
	me->fontSmallW = r.right;

	SelectObject( dc, me->fontNormal );
	DrawTextA( dc, "A", 1, &r, DT_SINGLELINE|DT_CALCRECT );
	me->fontNormalH = r.bottom;
	me->fontNormalW = r.right;

	SelectObject( dc, me->fontLarge );
	DrawTextA( dc, "A", 1, &r, DT_SINGLELINE|DT_CALCRECT );
	me->fontLargeH = r.bottom;
	me->fontLargeW = r.right;

	DeleteObject( bmp );
	DeleteDC( dc );

	me->systemInterface->registerForPacket( me->systemInterface, REMO_SYSTEM_PLAYER, REMO_PACKET_PLAYER_POSITION, onPlayerPosition, me );

	if ( me->frametimeInterface != NULL ) {
		me->frametimeInterface->registerOnPulse( me->frametimeInterface, onPulseData, me );
		me->frametimeInterface->registerOnMain( me->frametimeInterface, onMainTimeData, me );
		me->frametimeInterface->registerOnRenderTime( me->frametimeInterface, onRenderTimeData, me );
		me->frametimeInterface->registerOnGPUTime( me->frametimeInterface, onGPUTimeData, me );
	}

	if ( me->renderInterface != NULL ) {
		me->renderInterface->registerOnResolutionScaling( me->renderInterface, onResolutionScaling, me );
		me->renderInterface->registerOnDimensions( me->renderInterface, onDimensions, me );
	}

	if ( me->platformInterface != NULL ) {
		me->platformInterface->registerOnPlatformInfo( me->platformInterface, onPlatformInfo, me );
		me->platformInterface->registerOnBuildInfo( me->platformInterface, onBuildInfo, me );
		me->platformInterface->registerOnGraphicsInfo( me->platformInterface, onGraphicsInfo, me );
		me->platformInterface->registerOnMapLoad( me->platformInterface, onMapLoad, me );
		me->platformInterface->registerOnProcessorInfo( me->platformInterface, onProcessorInfo, me );
	}
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	frametimeUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	if ( me->platformInterface != NULL ) {
		me->platformInterface->unregisterOnProcessorInfo( me->platformInterface, onProcessorInfo, me );
		me->platformInterface->unregisterOnMapLoad( me->platformInterface, onMapLoad, me );
		me->platformInterface->unregisterOnGraphicsInfo( me->platformInterface, onGraphicsInfo, me );
		me->platformInterface->unregisterOnBuildInfo( me->platformInterface, onBuildInfo, me );
		me->platformInterface->unregisterOnPlatformInfo( me->platformInterface, onPlatformInfo, me );
	}

	if ( me->renderInterface != NULL ) {
		me->renderInterface->unregisterOnDimensions( me->renderInterface, onDimensions, me );
		me->renderInterface->unregisterOnResolutionScaling( me->renderInterface, onResolutionScaling, me );
	}

	if ( me->frametimeInterface != NULL ) {
		me->frametimeInterface->unregisterOnGPUTime( me->frametimeInterface, onGPUTimeData, me );
		me->frametimeInterface->unregisterOnRenderTime( me->frametimeInterface, onRenderTimeData, me );
		me->frametimeInterface->unregisterOnMain( me->frametimeInterface, onMainTimeData, me );
		me->frametimeInterface->unregisterOnPulse( me->frametimeInterface, onPulseData, me );
	}

	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_SYSTEM_PLAYER, REMO_PACKET_PLAYER_POSITION, onPlayerPosition, me );

	DeleteObject( me->fontLarge );
	me->fontLarge = 0;

	DeleteObject( me->fontNormal );
	me->fontNormal = 0;

	DeleteObject( me->fontSmall );
	me->fontSmall = 0;

	me->systemInterface->destroyWindow( me->systemInterface, me->wnd );
	me->wnd = 0;

	reset( me );

	me->platformInterface	= 0;
	me->frametimeInterface	= 0;
	me->renderInterface		= 0;
	me->timeInterface		= 0;
	me->systemInterface		= 0;
}

/*
========================
myRender
========================
*/
static void myRender( struct pluginInterface_type * const self ) {
	static const int borderPad = 2;

	RECT r;
	RECT r2;
	HDC dc;
	HGDIOBJ old;

	frametimeUIData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	GetClientRect( me->wnd, &r );

	dc = GetDC( me->wnd );

	SetBkMode( dc, TRANSPARENT );
	SetTextColor( dc, 0x000000 );

	FillRect( dc, &r, GetStockObject( LTGRAY_BRUSH ) );

	r.top += borderPad;
	r.left += borderPad;
	r.right -= borderPad;
	r.bottom -= borderPad;

	/* todo, select a nicer looking font... */

	r2 = r;
	DrawText( dc, "A", 1, &r2, DT_CALCRECT );

	old = SelectObject( dc, GetStockObject( BLACK_PEN ) );
	MoveToEx( dc, r2.left, r2.top, 0 );
	LineTo( dc, r2.right, r2.top );
	LineTo( dc, r2.right, r2.bottom );
	LineTo( dc, r2.left, r2.bottom );
	LineTo( dc, r2.left, r2.top );

	SelectObject( dc, old );

	ReleaseDC( me->wnd, dc );
}

/*
========================
myOnDisconnected
========================
*/
static void myOnDisconnected( struct pluginInterface_type * const self ) {
	frametimeUIData_t * const me = pluginInterfaceToMe( self );


	if ( me == NULL ) {
		return;
	}

	writeBitmap( me );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return "FrameTime_UI";
}

/*
========================
FrametimeUI_Create
========================
*/
struct pluginInterface_type * FrametimeUI_Create( struct systemInterface_type * const sys ) {
	frametimeUIData_t * const me = ( frametimeUIData_t* )sys->allocate( sys, sizeof( frametimeUIData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( frametimeUIData_t ) );

	me->checksum = PLUGIN_FRAMETIME_UI_CHECKSUM;

	me->pluginInterface.start			= myStart;
	me->pluginInterface.stop			= myStop;
	me->pluginInterface.render			= myRender;
	me->pluginInterface.onDisconnected	= myOnDisconnected;
	me->pluginInterface.getName			= myGetName;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
