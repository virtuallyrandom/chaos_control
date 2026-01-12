/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "time.h"
#include "plugin.h"

#define PLUGIN_TIME_CHECKSUM 0x54494D4554494D45 /* 'TIMETIME' */

static const uint16_t SYSTEM_INIT = 0;
static const uint16_t PACKET_INIT = 0;

const char PLUGIN_NAME_TIME[] = "Time";

typedef struct _timeData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct timeInterface_type		timeInterface;
	struct systemInterface_type *	systemInterface;
	double							clockFrequency;
} timeData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static timeData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_TIME_CHECKSUM ) {
			return ( timeData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
timeInterfaceToMe
========================
*/
static timeData_t* timeInterfaceToMe( struct timeInterface_type * const iface ) {
	const uint8_t * const ptrToTimeInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToTimeInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_TIME_CHECKSUM ) {
		return ( timeData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
convertTimeToClocks
========================
*/
static double convertTimeToClocks( struct timeInterface_type * const self, const uint64_t time) {
	( void )self;
	return ( double )time;
}

/*
========================
convertTimeToMicroseconds
========================
*/
static double convertTimeToMicroseconds( struct timeInterface_type * const self, const uint64_t time) {
	timeData_t * const me = timeInterfaceToMe( self );
	if ( me == NULL ) {
		return 0.0;
	}
	return ( time * 1000000.0 ) / max( 1.0f, me->clockFrequency );
}

/*
========================
convertTimeToMilliseconds
========================
*/
static double convertTimeToMilliseconds( struct timeInterface_type * const self, const uint64_t time) {
	timeData_t * const me = timeInterfaceToMe( self );
	if ( me == NULL ) {
		return 0.0;
	}
	return ( time * 1000.0 ) / max( 1.0f, me->clockFrequency );
}

/*
========================
convertTimeToSeconds
========================
*/
static double convertTimeToSeconds( struct timeInterface_type * const self, const uint64_t time) {
	timeData_t * const me = timeInterfaceToMe( self );
	if ( me == NULL ) {
		return 0.0;
	}
	return ( double )time / max( 1.0f, me->clockFrequency );
}

/*
========================
convertTimeToSecondsForFrequency
========================
*/
static double convertTimeToClocksForFrequency( struct timeInterface_type * const self, const uint64_t time, const uint64_t cpuFrequency ) {
	( void )self;
	( void )cpuFrequency;
	return ( double )time;
}

/*
========================
convertTimeToSecondsForFrequency
========================
*/
static double convertTimeToMicrosecondsForFrequency( struct timeInterface_type * const self, const uint64_t time, const uint64_t cpuFrequency ) {
	( void )self;
	return ( ( time * 1000.0 ) / max( 1.0f, cpuFrequency ) );
}

/*
========================
convertTimeToSecondsForFrequency
========================
*/
static double convertTimeToMillisecondsForFrequency( struct timeInterface_type * const self, const uint64_t time, const uint64_t cpuFrequency ) {
	( void )self;
	return ( ( time * 1000.0 ) / max( 1.0f, cpuFrequency ) ) * 1000.0;
}

/*
========================
convertTimeToSecondsForFrequency
========================
*/
static double convertTimeToSecondsForFrequency( struct timeInterface_type * const self, const uint64_t time, const uint64_t cpuFrequency ) {
	( void )self;
	return ( ( time * 1000.0 ) / max( 1.0f, cpuFrequency ) ) * 1000000.0;
}

/*
========================
onClockFrequency
========================
*/
static void onInitPacket( void * const param, const struct packetHeader_type * const header ) {
	struct remoInitPacket_t {
		struct packetHeader_type header;
		uint64_t clocksPerSecond;
	} * const pkt = ( struct remoInitPacket_t* )header;
	timeData_t * const me = ( timeData_t* )param;
	if ( me != NULL ) {
		me->clockFrequency = max( 1.0f, ( double )pkt->clocksPerSecond );
	}
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	timeData_t * const me = pluginInterfaceToMe( self );
	if ( me != NULL ) {
		me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_INIT, PACKET_INIT, onInitPacket, me );
	}
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	timeData_t * const me = pluginInterfaceToMe( self );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_INIT, PACKET_INIT, onInitPacket, me );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_TIME;
}

/*
========================
MemoryGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	timeData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return &me->timeInterface;
}

/*
========================
MemoryGetPluginInterface
========================
*/
struct pluginInterface_type * Time_Create( struct systemInterface_type * const sys ) {
	timeData_t * const me = ( timeData_t* )sys->allocate( sys, sizeof( timeData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( timeData_t ) );

	me->checksum = PLUGIN_TIME_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->timeInterface.convertTimeToClocks					= convertTimeToClocks;
	me->timeInterface.convertTimeToMicroseconds				= convertTimeToMicroseconds;
	me->timeInterface.convertTimeToMilliseconds				= convertTimeToMilliseconds;
	me->timeInterface.convertTimeToSeconds					= convertTimeToSeconds;
	me->timeInterface.convertTimeToClocksForFrequency		= convertTimeToClocksForFrequency;
	me->timeInterface.convertTimeToMicrosecondsForFrequency	= convertTimeToMicrosecondsForFrequency;
	me->timeInterface.convertTimeToMillisecondsForFrequency	= convertTimeToMillisecondsForFrequency;
	me->timeInterface.convertTimeToSecondsForFrequency		= convertTimeToSecondsForFrequency;

	me->systemInterface = sys;

	me->clockFrequency = 1.0f;

	return &me->pluginInterface;
}
