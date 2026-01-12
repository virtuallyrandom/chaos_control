/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "plugin_thread.h"
#include "plugin.h"

#define PLUGIN_THREAD_CHECKSUM 0x5448524541444544 /* 'THREADED' */

static const uint16_t SYSTEM_THREAD = 7;
static const uint16_t PACKET_CREATE = 1;
static const uint16_t PACKET_DESTROY = 2;

const char PLUGIN_NAME_THREAD[] = "Thread";

struct threadInfo_t {
	uint64_t id;
	const char * name;
	uint32_t stackSizeKB;
	uint16_t coreNumber;
	uint16_t padding;
};

typedef struct _threadData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	threadInterface_t				threadInterface;
	struct systemInterface_type *	systemInterface;
	struct threadInfo_t *			threadList;
	size_t							threadCount;
	size_t							threadSize;
} threadData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static threadData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_THREAD_CHECKSUM ) {
			return ( threadData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
threadInterfaceToMe
========================
*/
static threadData_t* threadInterfaceToMe( struct _threadInterface_t * const iface ) {
	const uint8_t * const ptrToThreadInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToThreadInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_THREAD_CHECKSUM ) {
		return ( threadData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
getThreadName
========================
*/
static const char * getThreadName( struct _threadInterface_t * const self, const uint64_t id ) {
	threadData_t * const me = threadInterfaceToMe( self );
	size_t i;
	for ( i = 0; i < me->threadCount; ++i ) {
		if ( me->threadList[ i ].id == id ) {
			return me->threadList[ i ].name;
		}
	}
	return NULL;
}

/*
========================
onThreadCreate
========================
*/
static void onThreadCreate( void * const param, const struct packetHeader_type * const header ) {
	const int growthStep = 16;
	struct remoThreadCreatePkt_t {
		struct packetHeader_type header;
		uint64_t threadID;
		uint32_t stackSizeKB;
		uint16_t core;
		uint16_t name;
	} * const pkt = ( struct remoThreadCreatePkt_t* )header;
	threadData_t * const me = ( threadData_t* )param;
	size_t i;
	struct threadInfo_t * threadInfo;

	if ( me->threadList == NULL ) {
		me->threadList = me->systemInterface->allocate( me->systemInterface, growthStep * sizeof( struct threadInfo_t ) );
		me->threadCount = 0;
		me->threadSize = growthStep;
	}

	threadInfo = NULL;

	/* find an existing thread with the same or no id (only on double create notifications) */
	for ( i = 0; i < me->threadCount; ++i ) {
		if ( me->threadList[ i ].id == pkt->threadID ) {
			threadInfo = &me->threadList[ i ];
			break;
		} else if ( threadInfo == NULL && me->threadList[ i ].id == 0 ) {
			threadInfo = &me->threadList[ i ];
		}
	}

	/* no available slot, so acquire one, growing if necessary */
	if ( threadInfo == NULL ) {
		if ( me->threadCount == me->threadSize ) {
			const size_t newCount = me->threadSize + growthStep;
			const size_t newSize = newCount * sizeof( struct threadInfo_t );
			me->threadList = me->systemInterface->reallocate( me->systemInterface, me->threadList, newSize );
			me->threadSize = newCount;
		}
		threadInfo = &me->threadList[ me->threadCount++ ];
	}

	threadInfo->id = pkt->threadID;
	threadInfo->name = me->systemInterface->findStringByID( me->systemInterface, pkt->name );
	threadInfo->stackSizeKB = pkt->stackSizeKB;
	threadInfo->coreNumber = pkt->core;
	threadInfo->padding = 0;
}

/*
========================
onThreadDestroy
========================
*/
static void onThreadDestroy( void * const param, const struct packetHeader_type * const header ) {
	struct pkt_t {
		struct packetHeader_type header;
		uint64_t threadID;
	} * const pkt = ( struct pkt_t* )header;
	threadData_t * const me = ( threadData_t* )param;
	size_t i;

	for ( i = me->threadCount; i < me->threadCount; ++i ) {
		if ( me->threadList[ i ].id == pkt->threadID ) {
			me->threadList[ i ].id = 0;
		}
	}
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	threadData_t * const me = pluginInterfaceToMe( self );
	if ( me != NULL ) {
		me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_THREAD, PACKET_CREATE, onThreadCreate, me );
		me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_THREAD, PACKET_DESTROY, onThreadDestroy, me );
	}
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	threadData_t * const me = pluginInterfaceToMe( self );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_THREAD, PACKET_DESTROY, onThreadDestroy, me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_THREAD, PACKET_CREATE, onThreadCreate, me );
	me->systemInterface->deallocate( me->systemInterface, me->threadList );
	me->threadList = NULL;
	me->threadCount = 0;
	me->threadSize = 0;
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_THREAD;
}

/*
========================
myGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	threadData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return &me->threadInterface;
}

/*
========================
Thread_Create
========================
*/
struct pluginInterface_type * Thread_Create( struct systemInterface_type * const sys ) {
	threadData_t * const me = ( threadData_t* )sys->allocate( sys, sizeof( threadData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( threadData_t ) );

	me->checksum = PLUGIN_THREAD_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->threadInterface.getThreadName = getThreadName;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
