/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "player.h"
#include "plugin.h"

#define PLUGIN_PLAYER_CHECKSUM 0x504c415952504f53 /* 'PLAYRPOS' */

#define MAX_CALLBACKS 32

#define REMO_PLAYER_SYSTEM_ID 4
#define REMO_PLAYER_PACKET_POSITION 1

typedef struct _playerPositionCallback_t {
	onPlayerPositionCallback_t	cb;
	void*						param;
} playerPositionCallback_t;

typedef struct _playerData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct playerInterface_type		playerInterface;
	struct systemInterface_type *	systemInterface;
	playerPositionCallback_t		playerPositionCB[ MAX_CALLBACKS ];
	size_t							playerPositionCount;
} playerData_t;

const char PLUGIN_NAME_PLAYER[] = "Player";

/*
========================
pluginInterfaceToMe
========================
*/
static playerData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_PLAYER_CHECKSUM ) {
			return ( playerData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
frametimeInterfaceToMe
========================
*/
static playerData_t* playerInterfaceToMe( struct playerInterface_type * const iface ) {
	const uint8_t * const ptrToPlayerInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToPlayerInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_PLAYER_CHECKSUM ) {
		return ( playerData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
myInitialize
========================
*/
static void onPlayerPosition( playerData_t * const me, const struct packetHeader_type * const header ) {
	struct playerPosition_type pos;
	size_t i;

	struct _pkt_t {
		struct packetHeader_type header;
		int8_t	playerID;
		uint8_t	angle[ 3 ];
		float	position[ 3 ];
	} * const pkt = ( struct _pkt_t* )header;

	pos.time = header->time;

	/* TODO: the old packet is quantizing incorrectly.  it needs to quantize a quaternion */
	pos.yaw = ( float )( ( ( int32_t )pkt->angle[ 0 ] * 2.0 / ( 1.0 / M_PI ) / UINT8_MAX ) - M_PI );
	pos.pitch = ( float )( ( ( int32_t )pkt->angle[ 1 ] * 2.0 / ( 1.0 / M_PI ) / UINT8_MAX ) - M_PI );
	pos.roll = ( float )( ( ( int32_t )pkt->angle[ 2 ] * 2.0 / ( 1.f / M_PI ) / UINT8_MAX ) - M_PI );

	pos.position[ 0 ] = pkt->position[ 0 ];
	pos.position[ 1 ] = pkt->position[ 1 ];
	pos.position[ 2 ] = pkt->position[ 2 ];

	for ( i = 0; i < me->playerPositionCount; ++i ) {
		playerPositionCallback_t * const call = me->playerPositionCB + i;
		if ( call->cb != NULL ) {
			call->cb( call->param, &pos );
		}
	}
}

/*
========================
registerOnBeginGameFrame
========================
*/
static void registerOnPlayerPosition( struct playerInterface_type * const self, onPlayerPositionCallback_t cb, void * const param ) {
	playerData_t * const me = playerInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	if ( me->playerPositionCount == MAX_CALLBACKS ) {
		size_t i;

		for ( i = 0; i < MAX_CALLBACKS; ++i ) {
			playerPositionCallback_t * const call = me->playerPositionCB + i;
			if ( call->cb == NULL ) {
				call->cb = cb;
				call->param = param;
				return;
			}
		}
	} else {
		playerPositionCallback_t * const call = me->playerPositionCB + me->playerPositionCount++;
		call->cb = cb;
		call->param = param;
	}
}

/*
========================
unregisterOnBeginGameFrame
========================
*/
static void unregisterOnPlayerPosition( struct playerInterface_type * const self, onPlayerPositionCallback_t cb, void * const param ) {
	size_t i;
	playerData_t * const me = playerInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	for ( i = 0; i < me->playerPositionCount; ++i ) {
		playerPositionCallback_t * const call = me->playerPositionCB + i;
		if ( call->cb == cb && call->param == param ) {
			call->cb = NULL;
			call->param = NULL;
		}
	}
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	playerData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	me->systemInterface->registerForPacket( me->systemInterface, REMO_PLAYER_SYSTEM_ID, REMO_PLAYER_PACKET_POSITION, onPlayerPosition, me );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	playerData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return;
	}
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_PLAYER_SYSTEM_ID, REMO_PLAYER_PACKET_POSITION, onPlayerPosition, me );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_PLAYER;
}

/*
========================
myGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	playerData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return NULL;
	}

	return &me->playerInterface;
}
/*
========================
myGetPluginInterface
========================
*/
struct pluginInterface_type * Player_Create( struct systemInterface_type * const sys ) {
	playerData_t * const me = sys->allocate( sys, sizeof( playerData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( playerData_t ) );

	me->checksum = PLUGIN_PLAYER_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->playerInterface.registerOnPlayerPosition	= registerOnPlayerPosition;
	me->playerInterface.unregisterOnPlayerPosition	= unregisterOnPlayerPosition;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
