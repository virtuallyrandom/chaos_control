/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "time.h"
#include "frametime.h"
#include "plugin.h"

#define PLUGIN_FRAMETIME_CHECKSUM 0x4652414d54494d45 /* 'FRAMTIME' */

#define MAX_CALLBACKS			32
#define MAX_THREADS				64
#define MAX_FRAME_NAME_LENGTH	128

#define REMO_FRAMETIME_SYSTEM_ID 3

#define REMO_FRAMETIME_PACKET_GAME_FRAME_BEGIN		1
#define REMO_FRAMETIME_PACKET_GAME_FRAME_END		2
#define REMO_FRAMETIME_PACKET_RENDER_FRAME_BEGIN	3
#define REMO_FRAMETIME_PACKET_RENDER_FRAME_END		4
#define REMO_FRAMETIME_PACKET_PULSE					5
#define REMO_FRAMETIME_PACKET_MAIN					6
#define REMO_PACKET_FRAMETIME_RENDER_TIME_NAME		7
#define REMO_PACKET_FRAMETIME_RENDER_TIME			8
#define REMO_PACKET_FRAMETIME_GPU_TIME				9

typedef void ( *genericCB_t )( void * const param );

typedef struct _frametimeBeginGameFrame_t {
	struct packetHeader_type header;
} frametimeBeginGameFrame_t;

typedef struct _frametimeEndGameFrame_t {
	struct packetHeader_type header;
} frametimeEndGameFrame_t;

typedef struct _frametimeBeginRenderFrame_t {
	struct packetHeader_type header;
} frametimeBeginRenderFrame_t;

typedef struct _frametimeEndRenderFrame_t {
	struct packetHeader_type header;
} frametimeEndRenderFrame_t;

typedef struct _frametimeCallbackInfo_t {
	genericCB_t	cb;
	void*		param;
} frametimeCallbackInfo_t;

typedef struct _timePkt_t {
	struct packetHeader_type header;
	uint32_t time[ MAX_FRAME_TIME_ENTRIES ];
} timePkt_t;

const char PLUGIN_NAME_FRAMETIME[] = "Frame Time";

/*
	TEMPORARY, until I get the remo side working, then main time will be reported like renderLog
*/
static timeInfo_t globalMainTimeInfo[ MAX_FRAME_TIME_ENTRIES ] = {		/*	12000	16000 */
	{ "MTT_SPIN",			0,	0x00,	0x00,	0x88,	0,				50,		500		},
	{ "MTT_PREGAME",		1,	0x00,	0x88,	0x00,	0,				50,		200		},
	{ "MTT_RUNFRAME",		2,	0x88,	0x00,	0x00,	0,				8000,	10000	},
	{ "MTT_IMAGESTREAMING",	3,	0x00,	0x00,	0x00,	0,				0,		0		},
	{ "MTT_GUI",			4,	0x00,	0x00,	0xff,	0,				750,	1000	},
	{ "MTT_SOUND",			5,	0x00,	0xff,	0x00,	0,				25,		100		},
	{ "MTT_POSTGAME",		6,	0xff,	0x00,	0x00,	0,				250,	500		},
	{ "MTT_SESSIONPUMP",	7,	0x00,	0x88,	0x88,	0,				750,	1000	},
	{ "MTT_RENDERSYNC",		8,	0x88,	0x88,	0x00,	0,				100,	250		},
	{ "MTT_INPUT",			9,	0x88,	0x00,	0x88,	0,				25,		100		},
	{ "MTT_DEV",			10,	0x00,	0x00,	0x00,	0,				0,		0,		},
	{ "MTT_RENDERENDFRAME",	11,	0x00,	0xff,	0xff,	0,				2000,	3000	},
	{ "MTT_DEBUGGUI",		12,	0xff,	0xff,	0x88,	0,				0,		0		},
	{ "MTT_SOUNDWAIT",		13,	0xff,	0x00,	0xff,	0,				0,		0		},
};
static uint32_t globalMainTimeInfoCount = sizeof( globalMainTimeInfo ) / sizeof( globalMainTimeInfo[ 0 ] ); /* FIXME */

static timeInfo_t globalPulseTimeInfo[ MAX_FRAME_TIME_ENTRIES ] = {
	{ "Main Thread",		0,	0xc5,	 0x3e,	 0x00,	0,				14000,	17000	},
	{ "Game Thread",		1,	0x26,	 0x84,	 0xb9,	0,				8000,	10000	},
	{ "Render Thread",		2,	0x33,	 0xbe,	 0x93,	0,				12000,	14000	},
	{ "GPU",				3,	0xd6,	 0xaf,	 0x00,	0,				15000,	16667	},
};
static uint32_t globalPulseTimeInfoCount = 4; /* FIXME */


typedef struct _frametimeData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct frametimeInterface_type	frametimeInterface;
	struct systemInterface_type *	systemInterface;
	struct timeInterface_type *		timeInterface;
	frametimeCallbackInfo_t			onBeginGameFrameCB[ MAX_CALLBACKS ];
	size_t							onBeginGameFrameCount;
	frametimeCallbackInfo_t			onEndGameFrameCB[ MAX_CALLBACKS ];
	size_t							onEndGameFrameCount;
	frametimeCallbackInfo_t			onBeginRenderFrameCB[ MAX_CALLBACKS ];
	size_t							onBeginRenderFrameCount;
	frametimeCallbackInfo_t			onEndRenderFrameCB[ MAX_CALLBACKS ];
	size_t							onEndRenderFrameCount;
	frametimeCallbackInfo_t			onPulseCB[ MAX_CALLBACKS ];
	size_t							onPulseCount;
	frametimeCallbackInfo_t			onMainCB[ MAX_CALLBACKS ];
	size_t							onMainCount;
	frametimeCallbackInfo_t			onRenderTimeCB[ MAX_CALLBACKS ];
	size_t							onRenderTimeCount;
	frametimeCallbackInfo_t			onGPUTimeCB[ MAX_CALLBACKS ];
	size_t							onGPUTimeCount;
	char							renderName[ MAX_FRAME_TIME_ENTRIES ][ MAX_FRAME_NAME_LENGTH ];
	timeInfo_t						renderTimeInfo[ MAX_FRAME_TIME_ENTRIES ];
	size_t							renderTimeInfoCount;
	frameTime_t						renderTime;
	frameTime_t						gpuTime;
	frameTime_t						mainTime;
	frameTime_t						pulseTime;
	timeInfo_t*						mainTimeInfo;
	size_t							mainTimeInfoCount;
	timeInfo_t*						pulseTimeInfo;
	size_t							pulseTimeInfoCount;
} frametimeData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static frametimeData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_FRAMETIME_CHECKSUM ) {
			return ( frametimeData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
frametimeInterfaceToMe
========================
*/
static frametimeData_t* frametimeInterfaceToMe( struct frametimeInterface_type * const iface ) {
	const uint8_t * const ptrToFrametimeInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToFrametimeInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_FRAMETIME_CHECKSUM ) {
		return ( frametimeData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
onPacketPulse
========================
*/
static void onPacketPulse( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	const timePkt_t * const pkt = ( const timePkt_t* )header;
	uint32_t i;

	if ( header->size == 48 ) {
		/* older packet style organized as 4 uint64_t's in microseconds */
		const uint64_t * const clocks = ( uint64_t* )( header + 1 );
		const size_t headerSize = sizeof( struct packetHeader_type );
		me->pulseTime.count = ( uint32_t )( ( header->size - headerSize ) / sizeof( uint64_t ) );

		for ( i = 0; i < me->pulseTime.count; ++i ) {
			me->pulseTime.entry[ i ] = ( uint32_t )clocks[ i ];
		}
	} else {
		/* new packet style, organized as a dynamic list of uint32_t's of microseconds */
		const uint32_t size = ( uint32_t )( header->size - sizeof( struct packetHeader_type ) );
		const uint32_t count = ( uint32_t )min( MAX_FRAME_TIME_ENTRIES, size / sizeof( uint32_t ) );

		me->pulseTime.count = count;
		for ( i = 0; i < count; ++i ) {
			me->pulseTime.entry[ i ] = pkt->time[ i ];
		}
	}

	for ( i = 0; i < me->onPulseCount; ++i ) {
		frametimeCallbackInfo_t * const call = me->onPulseCB + i;
		if ( call->cb ) {
			( ( onFrametimeTimeCallback )call->cb )( call->param, &me->pulseTime );
		}
	}
}

/*
========================
onPacketMain
========================
*/
static void onPacketMain( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	const size_t headerSize = sizeof( struct packetHeader_type );
	const timePkt_t * const pkt = ( const timePkt_t* )header;
	uint32_t i;

	if ( header->size == 96 ) {
		/* older packet style organized as 10 uint64_t's in clock cycles */
		const uint64_t * const clocks = ( uint64_t* )( header + 1 );
		me->mainTime.count = ( uint32_t )( ( header->size - headerSize ) / sizeof( uint64_t ) );

		for ( i = 0; i < me->mainTime.count; ++i ) {
			me->mainTime.entry[ i ] = ( uint32_t )me->timeInterface->convertTimeToMicroseconds( me->timeInterface, clocks[ i ] );
		}
	} else {
		/* new packet style, organized as a dynamic list of uint32_t's of microseconds */
		const uint32_t size = ( uint32_t )( header->size - headerSize );
		const uint32_t count = ( uint32_t )min( MAX_FRAME_TIME_ENTRIES, size / sizeof( uint32_t ) );

		me->mainTime.count = count;
		for ( i = 0; i < count; ++i ) {
			me->mainTime.entry[ i ] = ( uint32_t )me->timeInterface->convertTimeToMicroseconds( me->timeInterface, pkt->time[ i ] );
		}
	}

	for ( i = 0; i < me->onMainCount; ++i ) {
		frametimeCallbackInfo_t * const call = me->onMainCB + i;
		if ( call->cb ) {
			( ( onFrametimeTimeCallback )call->cb )( call->param, &me->mainTime );
		}
	}
}

/*
========================
onPacketRenderTimeInfo
========================
*/
static void onPacketRenderTimeInfo( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	typedef struct _infoPkt_t {
		struct packetHeader_type header;
		uint8_t id;
		uint8_t red;
		uint8_t green;
		uint8_t blue;
		uint32_t thresholdTimeUS;
		uint32_t maxPassTimeUS;
		uint32_t minFailTimeUS;
		char str[ 8 ];
	} infoPkt_t;

	const infoPkt_t * const pkt = ( const infoPkt_t* )header;
	const size_t baseSize = sizeof( infoPkt_t ) - 8;
	const size_t length = header->size - baseSize;
	const size_t index = min( MAX_FRAME_TIME_ENTRIES - 1, pkt->id );
	timeInfo_t * const info = me->renderTimeInfo + index;

	me->renderTimeInfoCount = max( me->renderTimeInfoCount, pkt->id + 1U );
	strncpy( me->renderName[ index ], pkt->str, length );
	me->renderName[ index ][ length ] = 0;

	info->name				= me->renderName[ index ];
	info->id				= pkt->id;
	info->red				= pkt->red;
	info->green				= pkt->green;
	info->blue				= pkt->blue;
	info->thresholdTimeUS	= pkt->thresholdTimeUS;
	info->maxPassTimeUS		= pkt->maxPassTimeUS;
	info->minFailTimeUS		= pkt->minFailTimeUS;
}

/*
========================
onPacketRenderTime
========================
*/
static void onPacketRenderTime( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	const timePkt_t * const pkt = ( const timePkt_t* )header;
	const uint32_t size = ( uint32_t )( header->size - sizeof( struct packetHeader_type ) );
	const uint32_t count = ( uint32_t )min( MAX_FRAME_TIME_ENTRIES, size / sizeof( uint32_t ) );

	uint32_t i;

	me->renderTime.count = min( MAX_FRAME_TIME_ENTRIES, count );

	for ( i = 0; i < count; ++i ) {
		me->renderTime.entry[ i ] = pkt->time[ i ];
	}

	for ( i = 0; i < me->onMainCount; ++i ) {
		frametimeCallbackInfo_t * const call = me->onRenderTimeCB + i;
		if ( call->cb ) {
			( ( onFrametimeTimeCallback )call->cb )( call->param, &me->renderTime );
		}
	}
}

/*
========================
onPacketGPUTime
========================
*/
static void onPacketGPUTime( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	const timePkt_t * const pkt = ( const timePkt_t* )header;
	const uint32_t size = ( uint32_t )( header->size - sizeof( struct packetHeader_type ) );
	const uint32_t count = ( uint32_t )min( MAX_FRAME_TIME_ENTRIES, size / sizeof( uint32_t ) );

	uint32_t i;

	me->gpuTime.count = min( MAX_FRAME_TIME_ENTRIES, count );

	for ( i = 0; i < count; ++i ) {
		me->gpuTime.entry[ i ] = pkt->time[ i ];
	}

	for ( i = 0; i < me->onMainCount; ++i ) {
		frametimeCallbackInfo_t * const call = me->onGPUTimeCB + i;
		if ( call->cb ) {
			( ( onFrametimeTimeCallback )call->cb )( call->param, &me->gpuTime );
		}
	}
}

/*
========================
issueTimeEvent
========================
*/
static void issueTimeEvent(	const struct packetHeader_type * const header,
							frametimeCallbackInfo_t * const list,
							const size_t count ) {
	size_t i;

	for ( i = 0; i < count; ++i ) {
		frametimeCallbackInfo_t * const call = list + i;
		if ( call->cb ) {
			( ( onFrametimeEventCallback )call->cb )( call->param, header->time );
		}
	}
}

/*
========================
onBeginGameFrame
========================
*/
static void onBeginGameFrame( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	issueTimeEvent( header, me->onBeginGameFrameCB, me->onBeginGameFrameCount );
}

/*
========================
onEndGameFrame
========================
*/
static void onEndGameFrame( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	issueTimeEvent( header, me->onEndGameFrameCB, me->onEndGameFrameCount );
}

/*
========================
onBeginRenderFrame
========================
*/
static void onBeginRenderFrame( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	issueTimeEvent( header, me->onBeginRenderFrameCB, me->onBeginRenderFrameCount );
}

/*
========================
onEndRenderFrame
========================
*/
static void onEndRenderFrame( frametimeData_t * const me, const struct packetHeader_type * const header ) {
	issueTimeEvent( header, me->onEndRenderFrameCB, me->onEndRenderFrameCount );
}

/*
========================
registerEventHandler
========================
*/
static void registerEventHandler(	frametimeCallbackInfo_t * const list, 
									size_t * const count,
									genericCB_t cb,
									void * const param ) {
	if ( *count == MAX_CALLBACKS ) {
		size_t i;
		for ( i = 0; i < MAX_CALLBACKS; ++i ) {
			frametimeCallbackInfo_t * const call = list + i;
			if ( call->cb == NULL ) {
				call->cb = cb;
				call->param = param;
				return;
			}
		}
	} else {
		frametimeCallbackInfo_t * const call = list + *count;
		call->cb = cb;
		call->param = param;
		*count += 1;
	}
}

/*
========================
unregisterEventHandler
========================
*/
static void unregisterEventHandler(	frametimeCallbackInfo_t * const list,
									const size_t count,
									genericCB_t cb,
									void * const param ) {
	size_t i;
	for ( i = 0; i < count; ++i ) {
		frametimeCallbackInfo_t * const call = list + i;
		if ( call->cb == cb && call->param == param ) {
			call->cb = NULL;
			call->param = NULL;
		}
	}
}

/*
========================
registerOnBeginGameFrame
========================
*/
static void registerOnBeginGameFrame( struct frametimeInterface_type * const self, onFrametimeEventCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onBeginGameFrameCB, &me->onBeginGameFrameCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnBeginGameFrame
========================
*/
static void unregisterOnBeginGameFrame( struct frametimeInterface_type * const self, onFrametimeEventCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onBeginGameFrameCB, me->onBeginGameFrameCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnEndGameFrame
========================
*/
static void registerOnEndGameFrame( struct frametimeInterface_type * const self, onFrametimeEventCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onEndGameFrameCB, &me->onEndGameFrameCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnBeginGameFrame
========================
*/
static void unregisterOnEndGameFrame( struct frametimeInterface_type * const self, onFrametimeEventCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onEndGameFrameCB, me->onEndGameFrameCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnBeginRenderFrame
========================
*/
static void registerOnBeginRenderFrame( struct frametimeInterface_type * const self, onFrametimeEventCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onBeginRenderFrameCB, &me->onBeginRenderFrameCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnBeginRenderFrame
========================
*/
static void unregisterOnBeginRenderFrame( struct frametimeInterface_type * const self, onFrametimeEventCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onBeginRenderFrameCB, me->onBeginRenderFrameCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnEndRenderFrame
========================
*/
static void registerOnEndRenderFrame( struct frametimeInterface_type * const self, onFrametimeEventCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onEndRenderFrameCB, &me->onEndRenderFrameCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnBeginRenderFrame
========================
*/
static void unregisterOnEndRenderFrame( struct frametimeInterface_type * const self, onFrametimeEventCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onEndRenderFrameCB, me->onEndRenderFrameCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnPulse
========================
*/
static void registerOnPulse( struct frametimeInterface_type * const self, onFrametimeTimeCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onPulseCB, &me->onPulseCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnPulse
========================
*/
static void unregisterOnPulse( struct frametimeInterface_type * const self, onFrametimeTimeCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onPulseCB, me->onPulseCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnMain
========================
*/
static void registerOnMain( struct frametimeInterface_type * const self, onFrametimeTimeCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onMainCB, &me->onMainCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnMain
========================
*/
static void unregisterOnMain( struct frametimeInterface_type * const self, onFrametimeTimeCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onMainCB, me->onMainCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnRenderTime
========================
*/
static void registerOnRenderTime( struct frametimeInterface_type * const self, onFrametimeTimeCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onRenderTimeCB, &me->onRenderTimeCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnRenderTime
========================
*/
static void unregisterOnRenderTime( struct frametimeInterface_type * const self, onFrametimeTimeCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onRenderTimeCB, me->onRenderTimeCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnGPUTime
========================
*/
static void registerOnGPUTime( struct frametimeInterface_type * const self, onFrametimeTimeCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onGPUTimeCB, &me->onGPUTimeCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnGPUrTime
========================
*/
static void unregisterOnGPUTime( struct frametimeInterface_type * const self, onFrametimeTimeCallback cb, void * const param ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onGPUTimeCB, me->onGPUTimeCount, ( genericCB_t )cb, param );
}

/*
========================
myGetPulseTimeInfo
========================
*/
static const timeInfo_t* myGetPulseTimeInfo( struct frametimeInterface_type * const self, size_t * const optionalCount ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	if ( optionalCount != 0 ) {
		*optionalCount = ( size_t )me->pulseTimeInfoCount;
	}
	return me->pulseTimeInfo;
}

/*
========================
myGetMainTimeInfo
========================
*/
static const timeInfo_t* myGetMainTimeInfo( struct frametimeInterface_type * const self, size_t * const optionalCount ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	if ( optionalCount != 0 ) {
		*optionalCount = ( size_t )me->mainTimeInfoCount;
	}
	return me->mainTimeInfo;
}

/*
========================
myGetRenderTimeInfo
========================
*/
static const timeInfo_t* myGetRenderTimeInfo( struct frametimeInterface_type * const self, size_t * const optionalCount ) {
	frametimeData_t * const me = frametimeInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	if ( optionalCount != 0 ) {
		*optionalCount = ( size_t )me->renderTimeInfoCount;
	}
	return me->renderTimeInfo;
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	frametimeData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->timeInterface = me->systemInterface->getPrivateInterface( me->systemInterface, PLUGIN_NAME_TIME );

	/* these are "temporary" as in one day I may put them in the game... because that's where
	   they should be located in the first place... */
	me->mainTimeInfo		= globalMainTimeInfo;
	me->mainTimeInfoCount	= globalMainTimeInfoCount;
	me->pulseTimeInfo		= globalPulseTimeInfo;
	me->pulseTimeInfoCount	= globalPulseTimeInfoCount;

	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_GAME_FRAME_BEGIN,		onBeginGameFrame,		me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_GAME_FRAME_END,		onEndGameFrame,			me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_RENDER_FRAME_BEGIN,	onBeginRenderFrame,		me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_RENDER_FRAME_END,		onEndRenderFrame,		me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_PULSE,					onPacketPulse,			me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_MAIN,					onPacketMain,			me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_PACKET_FRAMETIME_RENDER_TIME_NAME,		onPacketRenderTimeInfo,	me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_PACKET_FRAMETIME_RENDER_TIME,			onPacketRenderTime,		me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_PACKET_FRAMETIME_GPU_TIME,				onPacketGPUTime,		me );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	frametimeData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_PACKET_FRAMETIME_GPU_TIME,			onPacketGPUTime,		me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_PACKET_FRAMETIME_RENDER_TIME,			onPacketRenderTime,		me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_PACKET_FRAMETIME_RENDER_TIME_NAME,	onPacketRenderTimeInfo,	me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_MAIN,				onPacketMain,			me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_PULSE,				onPacketPulse,			me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_RENDER_FRAME_END,	onEndRenderFrame,		me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_RENDER_FRAME_BEGIN,	onBeginRenderFrame,		me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_GAME_FRAME_END,		onEndGameFrame,			me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_FRAMETIME_SYSTEM_ID, REMO_FRAMETIME_PACKET_GAME_FRAME_BEGIN,	onBeginGameFrame,		me );

	me->timeInterface = 0;
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_FRAMETIME;
}

/*
========================
myGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	frametimeData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return NULL;
	}

	return &me->frametimeInterface;
}

/*
========================
Frametime_Create
========================
*/
struct pluginInterface_type * Frametime_Create( struct systemInterface_type * const sys ) {
	frametimeData_t * const me = ( frametimeData_t* )sys->allocate( sys, sizeof( frametimeData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( frametimeData_t ) );

	me->checksum = PLUGIN_FRAMETIME_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->frametimeInterface.registerOnBeginGameFrame		= registerOnBeginGameFrame;
	me->frametimeInterface.unregisterOnBeginGameFrame	= unregisterOnBeginGameFrame;
	me->frametimeInterface.registerOnEndGameFrame		= registerOnEndGameFrame;
	me->frametimeInterface.unregisterOnEndGameFrame		= unregisterOnEndGameFrame;
	me->frametimeInterface.registerOnBeginRenderFrame	= registerOnBeginRenderFrame;
	me->frametimeInterface.unregisterOnBeginRenderFrame	= unregisterOnBeginRenderFrame;
	me->frametimeInterface.registerOnEndRenderFrame		= registerOnEndRenderFrame;
	me->frametimeInterface.unregisterOnEndRenderFrame	= unregisterOnEndRenderFrame;
	me->frametimeInterface.registerOnPulse				= registerOnPulse;
	me->frametimeInterface.unregisterOnPulse			= unregisterOnPulse;
	me->frametimeInterface.registerOnMain				= registerOnMain;
	me->frametimeInterface.unregisterOnMain				= unregisterOnMain;
	me->frametimeInterface.registerOnRenderTime			= registerOnRenderTime;
	me->frametimeInterface.unregisterOnRenderTime		= unregisterOnRenderTime;
	me->frametimeInterface.registerOnGPUTime			= registerOnGPUTime;
	me->frametimeInterface.unregisterOnGPUTime			= unregisterOnGPUTime;
	me->frametimeInterface.getPulseTimeInfo				= myGetPulseTimeInfo;
	me->frametimeInterface.getMainTimeInfo				= myGetMainTimeInfo;
	me->frametimeInterface.getRenderTimeInfo			= myGetRenderTimeInfo;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
