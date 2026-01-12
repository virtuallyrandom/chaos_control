/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "time.h"
#include "render.h"
#include "plugin.h"

#define PLUGIN_RENDER_CHECKSUM 0x52454E4445522020 /* 'RENDER  ' */

#define MAX_CALLBACKS			32

#define RENDER_SYSTEM_ID 5

#define PACKET_RESOLUTION_SCALING	1
#define PACKET_DIMENSIONS			2

typedef void ( *genericCB_t )( void * const param );

typedef struct _renderCallbackInfo_t {
	genericCB_t	cb;
	void*		param;
} renderCallbackInfo_t;

const char PLUGIN_NAME_RENDER[] = "Render";

typedef struct _renderData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct renderInterface_type		renderInterface;
	struct systemInterface_type *	systemInterface;
	int32_t							dimensionX;
	int32_t							dimensionY;
	renderCallbackInfo_t			onResolutionScalingCB[ MAX_CALLBACKS ];
	size_t							onResolutionScalingCount;
	renderCallbackInfo_t			onDimensionsCB[ MAX_CALLBACKS ];
	size_t							onDimensionsCount;
} renderData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static renderData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_RENDER_CHECKSUM ) {
			return ( renderData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
renderInterfaceToMe
========================
*/
static renderData_t* renderInterfaceToMe( struct renderInterface_type * const iface ) {
	const uint8_t * const ptrToRenderInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToRenderInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_RENDER_CHECKSUM ) {
		return ( renderData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
registerEventHandler
========================
*/
static void registerEventHandler(	renderCallbackInfo_t * const list, 
									size_t * const count,
									genericCB_t cb,
									void * const param ) {
	if ( *count == MAX_CALLBACKS ) {
		size_t i;
		for ( i = 0; i < MAX_CALLBACKS; ++i ) {
			renderCallbackInfo_t * const call = list + i;
			if ( call->cb == 0 ) {
				call->cb = cb;
				call->param = param;
				return;
			}
		}
	} else {
		renderCallbackInfo_t * const call = list + *count;
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
static void unregisterEventHandler(	renderCallbackInfo_t * const list,
									const size_t count,
									genericCB_t cb,
									void * const param ) {
	size_t i;
	for ( i = 0; i < count; ++i ) {
		renderCallbackInfo_t * const call = list + i;
		if ( call->cb == cb && call->param == param ) {
			call->cb = 0;
			call->param = 0;
		}
	}
}

/*
========================
registerOnResolutionScaling
========================
*/
static void registerOnResolutionScaling(	struct renderInterface_type * const self,
											onResolutionScalingCallback cb,
											void * const param ) {
	renderData_t * const me = renderInterfaceToMe( self );
	if ( me != NULL ) {
		registerEventHandler(	me->onResolutionScalingCB,
								&me->onResolutionScalingCount,
								( genericCB_t )cb,
								param );
	}
}

/*
========================
unregisterOnResolutionScaling
========================
*/
static void unregisterOnResolutionScaling(	struct renderInterface_type * const self,
											onResolutionScalingCallback cb,
											void * const param ) {
	renderData_t * const me = renderInterfaceToMe( self );
	if ( me != NULL ) {
		unregisterEventHandler(	me->onResolutionScalingCB,
								me->onResolutionScalingCount,
								( genericCB_t )cb,
								param );
	}
}

/*
========================
registerOnDimensions
========================
*/
static void registerOnDimensions(	struct renderInterface_type * const self,
									onDimensionsCallback cb,
									void * const param ) {
	renderData_t * const me = renderInterfaceToMe( self );
	if ( me != NULL ) {
		registerEventHandler(	me->onDimensionsCB,
								&me->onDimensionsCount,
								( genericCB_t )cb,
								param );
	}
}

/*
========================
unregisterOnDimensions
========================
*/
static void unregisterOnDimensions(	struct renderInterface_type * const self,
									onDimensionsCallback cb,
									void * const param ) {
	renderData_t * const me = renderInterfaceToMe( self );
	if ( me != NULL ) {
		unregisterEventHandler(	me->onDimensionsCB,
								me->onDimensionsCount,
								( genericCB_t )cb,
								param );
	}
}

/*
========================
onResolutionScaling
========================
*/
static void onResolutionScaling( void * const param, const struct packetHeader_type * const header ) {
	typedef struct _rs_t {
		struct packetHeader_type hdr;
		float x;
		float y;
	} rs_t;

	renderData_t * const me = ( renderData_t* )param;
	rs_t * const pkt = ( rs_t* )header;
	size_t i;

	for ( i = 0; i < me->onResolutionScalingCount; ++i ) {
		renderCallbackInfo_t * const call = me->onResolutionScalingCB + i;
		if ( call->cb ) {
			( ( onResolutionScalingCallback )call->cb )( call->param, pkt->x, pkt->y, pkt->hdr.time );
		}
	}
}

/*
========================
onDimensions
========================
*/
static void onDimensions( void * const param, const struct packetHeader_type * const header ) {
	typedef struct _dim_t {
		struct packetHeader_type hdr;
		uint16_t x;
		uint16_t y;
		uint32_t padding;
	} dim_t;

	renderData_t * const me = ( renderData_t* )param;
	dim_t * const pkt = ( dim_t* )header;

	const int changed = pkt->x != me->dimensionX || pkt->y != me->dimensionY;

	if ( changed ) {
		size_t i;

		me->dimensionX = pkt->x;
		me->dimensionY = pkt->y;

		for ( i = 0; i < me->onDimensionsCount; ++i ) {
			renderCallbackInfo_t * const call = me->onDimensionsCB + i;
			if ( call->cb ) {
				( ( onDimensionsCallback )call->cb )( call->param, pkt->x, pkt->y );
			}
		}
	}
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	renderData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	me->systemInterface->registerForPacket( me->systemInterface, RENDER_SYSTEM_ID, PACKET_RESOLUTION_SCALING,	onResolutionScaling,	me );
	me->systemInterface->registerForPacket( me->systemInterface, RENDER_SYSTEM_ID, PACKET_DIMENSIONS,			onDimensions,			me );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	renderData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	me->systemInterface->unregisterForPacket( me->systemInterface, RENDER_SYSTEM_ID, PACKET_DIMENSIONS,			onDimensions,			me );
	me->systemInterface->unregisterForPacket( me->systemInterface, RENDER_SYSTEM_ID, PACKET_RESOLUTION_SCALING,	onResolutionScaling,	me );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_RENDER;
}

/*
========================
myGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	renderData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return &me->renderInterface;
}

/*
========================
MemGraphGetPluginInterface
========================
*/
struct pluginInterface_type * Render_Create( struct systemInterface_type * const sys ) {
	renderData_t * const me = ( renderData_t* )sys->allocate( sys, sizeof( renderData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( renderData_t ) );

	me->checksum = PLUGIN_RENDER_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->renderInterface.registerOnResolutionScaling		= registerOnResolutionScaling;
	me->renderInterface.unregisterOnResolutionScaling	= unregisterOnResolutionScaling;
	me->renderInterface.registerOnDimensions			= registerOnDimensions;
	me->renderInterface.unregisterOnDimensions			= unregisterOnDimensions;

	me->systemInterface = sys;

	me->dimensionX = -1;
	me->dimensionY = -1;

	return &me->pluginInterface;
}
