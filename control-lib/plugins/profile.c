/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "profile.h"
#include "plugin.h"

#define PLUGIN_PROFILE_CHECKSUM 0x50524f46494c4520 /* 'PROFILE ' */

#define MAX_CALLBACKS				32
#define MAX_THREADS					64

#define REMO_SYSTEM_SYSTEM			0
#define REMO_PACKET_SYNC			11

#define REMO_SYSTEM_PROFILE			6
#define REMO_PACKET_PROFILE_ENTER0	1
#define REMO_PACKET_PROFILE_ENTER1	2
#define REMO_PACKET_PROFILE_ENTER2	3
#define REMO_PACKET_PROFILE_LEAVE	4

typedef struct _packetEnter0_t {
	struct packetHeader_type	header;
	uint64_t					threadID;
	uint64_t					categoryMask;
	uint16_t					label;
	uint8_t						padding[ 6 ];
} packetEnter0_t;

typedef struct _packetEnter1_t {
	struct packetHeader_type	header;
	uint64_t					threadID;
	uint64_t					categoryMask;
	char						label[ 128 ];
} packetEnter1_t;

typedef struct _packetEnter2_t {
	struct packetHeader_type	header;
	uint64_t					threadID;
	uint64_t					categoryMask;
	uint64_t					label;
} packetEnter2_t;

typedef struct _packetLeave_t {
	struct packetHeader_type	header;
	uint64_t					threadID;
} packetLeave_t;

typedef struct _syncCallbackInfo_t {
	onProfileSyncCallback	cb;
	void					*param;
} syncCallbackInfo_t;

typedef struct _enterCallbackInfo_t {
	onProfileEnterCallback	cb;
	void					*param;
} enterCallbackInfo_t;

typedef struct _leaveCallbackInfo_t {
	onProfileLeaveCallback	cb;
	void					*param;
} leaveCallbackInfo_t;

typedef struct _profileData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct profileInterface_type	profileInterface;
	struct systemInterface_type *	systemInterface;
	syncCallbackInfo_t				onSyncCB[ MAX_CALLBACKS ];
	size_t							onSyncCount;
	enterCallbackInfo_t				onEnterCB[ MAX_CALLBACKS ];
	size_t							onEnterCount;
	leaveCallbackInfo_t				onLeaveCB[ MAX_CALLBACKS ];
	size_t							onLeaveCount;
} profileData_t;

const char PLUGIN_NAME_PROFILE[] = "Profile";

/*
========================
pluginInterfaceToMe
========================
*/
static profileData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_PROFILE_CHECKSUM ) {
			return ( profileData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
profileInterfaceToMe
========================
*/
static profileData_t* profileInterfaceToMe( struct profileInterface_type * const iface ) {
	const uint8_t * const ptrToProfileInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToProfileInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_PROFILE_CHECKSUM ) {
		return ( profileData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
registerOnSync
========================
*/
static void registerOnSync( struct profileInterface_type * const iface, onProfileSyncCallback cb, void * const param ) {
	profileData_t * const me = profileInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	if ( me->onSyncCount == MAX_CALLBACKS ) {
		size_t i;
		for ( i = 0; i < MAX_CALLBACKS; ++i ) {
			syncCallbackInfo_t * const call = me->onSyncCB + i;
			if ( call->cb == NULL ) {
				call->cb = cb;
				call->param = param;
				return;
			}
		}
	} else {
		syncCallbackInfo_t * const call = me->onSyncCB + me->onSyncCount++;
		call->cb = cb;
		call->param = param;
	}
}

/*
========================
unregisterOnSync
========================
*/
static void unregisterOnSync( struct profileInterface_type * const iface, onProfileSyncCallback cb, void * const param ) {
	size_t i;
	profileData_t * const me = profileInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	for ( i = 0; i < me->onSyncCount; ++i ) {
		syncCallbackInfo_t * const call = me->onSyncCB + i;
		if ( call->cb == cb && call->param == param ) {
			call->cb = NULL;
			call->param = NULL;
		}
	}
}

/*
========================
registerOnEnter
========================
*/
static void registerOnEnter( struct profileInterface_type * const iface, onProfileEnterCallback cb, void * const param ) {
	profileData_t * const me = profileInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	if ( me->onEnterCount == MAX_CALLBACKS ) {
		size_t i;
		for ( i = 0; i < MAX_CALLBACKS; ++i ) {
			enterCallbackInfo_t * const call = me->onEnterCB + i;
			if ( call->cb == NULL ) {
				call->cb = cb;
				call->param = param;
				return;
			}
		}
	} else {
		enterCallbackInfo_t * const call = me->onEnterCB + me->onEnterCount++;
		call->cb = cb;
		call->param = param;
	}
}

/*
========================
unregisterOnEnter
========================
*/
static void unregisterOnEnter( struct profileInterface_type * const iface, onProfileEnterCallback cb, void * const param ) {
	size_t i;
	profileData_t * const me = profileInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	for ( i = 0; i < MAX_CALLBACKS; ++i ) {
		enterCallbackInfo_t * const call = me->onEnterCB + i;
		if ( call->cb == cb && call->param == param ) {
			call->cb = 0;
			call->param = 0;
		}
	}
}

/*
========================
registerOnLeave
========================
*/
static void registerOnLeave( struct profileInterface_type * const iface, onProfileLeaveCallback cb, void * const param ) {
	profileData_t * const me = profileInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	if ( me->onLeaveCount == MAX_CALLBACKS ) {
		size_t i;
		for ( i = 0; i < MAX_CALLBACKS; ++i ) {
			leaveCallbackInfo_t * const call = me->onLeaveCB + i;
			if ( call->cb == 0 ) {
				call->cb = cb;
				call->param = param;
				return;
			}
		}
	} else {
		leaveCallbackInfo_t * const call = me->onLeaveCB + me->onLeaveCount++;
		call->cb = cb;
		call->param = param;
	}
}

/*
========================
unregisterOnLeave
========================
*/
static void unregisterOnLeave( struct profileInterface_type * const iface, onProfileLeaveCallback cb, void * const param ) {
	size_t i;
	profileData_t * const me = profileInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	for ( i = 0; i < MAX_CALLBACKS; ++i ) {
		leaveCallbackInfo_t * const call = me->onLeaveCB + i;
		if ( call->cb == cb && call->param == param ) {
			call->cb = 0;
			call->param = 0;
		}
	}
}

/*
========================
onEnter0
========================
*/
static void onEnter0( void * const self, const struct packetHeader_type * const pkt ) {
	profileData_t * const me = ( profileData_t * )self;
	packetEnter0_t * const enterPkt = ( packetEnter0_t * )pkt;
	const char * const label = me->systemInterface->findStringByID( me->systemInterface, enterPkt->label );
	size_t i;
	for ( i = 0; i < me->onEnterCount; ++i ) {
		enterCallbackInfo_t * const cb = me->onEnterCB + i;
		if ( cb->cb != NULL ) {
			cb->cb( cb->param, enterPkt->threadID, enterPkt->header.time, enterPkt->categoryMask, label );
		}
	}
}

/*
========================
onEnter1
========================
*/
static void onEnter1( void * const self, const struct packetHeader_type * const pkt ) {
	profileData_t * const me = ( profileData_t * )self;
	packetEnter1_t * const enterPkt = ( packetEnter1_t * )pkt;
	size_t i;
	for ( i = 0; i < me->onEnterCount; ++i ) {
		enterCallbackInfo_t * const cb = me->onEnterCB + i;
		if ( cb->cb != NULL ) {
			cb->cb( cb->param, enterPkt->threadID, enterPkt->header.time, enterPkt->categoryMask, enterPkt->label );
		}
	}
}

/*
========================
onEnter2
========================
*/
static void onEnter2( void * const self, const struct packetHeader_type * const pkt ) {
	profileData_t * const me = ( profileData_t * )self;
	packetEnter2_t * const enterPkt = ( packetEnter2_t * )pkt;
	const char * const label = me->systemInterface->findStringByAddress( me->systemInterface, enterPkt->label );
	size_t i;
	for ( i = 0; i < me->onEnterCount; ++i ) {
		enterCallbackInfo_t * const cb = me->onEnterCB + i;
		if ( cb->cb != NULL ) {
			cb->cb( cb->param, enterPkt->threadID, enterPkt->header.time, enterPkt->categoryMask, label );
		}
	}
}

/*
========================
onLeave
========================
*/
static void onLeave( void * const self, const struct packetHeader_type * const pkt ) {
	profileData_t * const me = ( profileData_t * )self;
	packetLeave_t * const leavePkt = ( packetLeave_t * )pkt;
	size_t i;
	for ( i = 0; i < me->onLeaveCount; ++i ) {
		leaveCallbackInfo_t * const cb = me->onLeaveCB + i;
		if ( cb->cb != NULL ) {
			cb->cb( cb->param, leavePkt->threadID, leavePkt->header.time );
		}
	}
}

/*
========================
onSync
========================
*/
static void onSync( void * const self, const struct packetHeader_type * const pkt ) {
	profileData_t * const me = ( profileData_t * )self;
	size_t i;
	for ( i = 0; i < me->onSyncCount; ++i ) {
		syncCallbackInfo_t * const cb = me->onSyncCB + i;
		if ( cb->cb != NULL ) {
			cb->cb( cb->param, pkt->time );
		}
	}
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	profileData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->systemInterface->registerForPacket( me->systemInterface, REMO_SYSTEM_PROFILE, REMO_PACKET_PROFILE_ENTER0,	onEnter0,	me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_SYSTEM_PROFILE, REMO_PACKET_PROFILE_ENTER1,	onEnter1,	me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_SYSTEM_PROFILE, REMO_PACKET_PROFILE_ENTER2,	onEnter2,	me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_SYSTEM_PROFILE, REMO_PACKET_PROFILE_LEAVE,	onLeave,	me );
	me->systemInterface->registerForPacket( me->systemInterface, REMO_SYSTEM_SYSTEM, REMO_PACKET_SYNC,				onSync,		me );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	profileData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_SYSTEM_SYSTEM,	REMO_PACKET_SYNC,			onSync,		me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_SYSTEM_PROFILE, REMO_PACKET_PROFILE_LEAVE,	onLeave,	me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_SYSTEM_PROFILE, REMO_PACKET_PROFILE_ENTER2,	onEnter2,	me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_SYSTEM_PROFILE, REMO_PACKET_PROFILE_ENTER1,	onEnter1,	me );
	me->systemInterface->unregisterForPacket( me->systemInterface, REMO_SYSTEM_PROFILE, REMO_PACKET_PROFILE_ENTER0,	onEnter0,	me );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_PROFILE;
}

/*
========================
myGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	profileData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return &me->profileInterface;
}

/*
========================
Profile_Create
========================
*/
struct pluginInterface_type * Profile_Create( struct systemInterface_type * const sys ) {
	profileData_t * const me = ( profileData_t* )sys->allocate( sys, sizeof( profileData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( profileData_t ) );

	me->checksum = PLUGIN_PROFILE_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->profileInterface.registerOnSync		= registerOnSync;
	me->profileInterface.unregisterOnSync	= unregisterOnSync;
	me->profileInterface.registerOnEnter	= registerOnEnter;
	me->profileInterface.unregisterOnEnter	= unregisterOnEnter;
	me->profileInterface.registerOnLeave	= registerOnLeave;
	me->profileInterface.unregisterOnLeave	= unregisterOnLeave;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
