/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "platform.h"
#include "plugin.h"

#define PLUGIN_PLATFORM_CHECKSUM 0x504C4154464F524D /* 'PLATFORM' */

#define MAX_CALLBACKS		32
#define MAX_MAP_NAME_LENGTH	96
#define MAX_PROCESSORS		8
#define MAX_GRAPHICSCARDS	8

#define SYSTEM_SYSTEM		0

#define PACKET_PLATFORM		0
#define PACKET_BUILDINFO	3
#define PACKET_MAPLOAD		5
#define PACKET_PROCESSOR	6
#define PACKET_GRAPHICS		7

#define PACKET_GRAPHICS_DEPRECATED		4

const char PLUGIN_NAME_PLATFORM[] = "Platform";

typedef void ( *genericCB_t )( void * const param );

typedef struct _callbackInfo_t {
	genericCB_t	cb;
	void*		param;
} callbackInfo_t;

typedef struct _platformInfo_DEPRECATED_t {
	uint64_t	clockTicksPerSecond;
	struct {
		uint32_t year	: 6; /* [0..63], add 2000 for year  */
		uint32_t month	: 4; /* [0..15], add 1, 1 = January */
		uint32_t day	: 5; /* [0..31]                     */
		uint32_t hour	: 5; /* [0..31], add 1              */
		uint32_t minute	: 6; /* [0..63]                     */
		uint32_t second	: 6; /* [0..63]                     */
	} timeInfo; /* 32b */
	uint16_t	cpuid;
	uint16_t	systemRAMMB;
	uint16_t	videoRAMMB;
	uint8_t		cpuPhysicalCount;
	uint8_t		cpuLogicalCount;
	uint8_t		cpuPackageCount;
	uint8_t		platform;
	uint8_t		binaryType;
	uint8_t		renderType;
	uint8_t		productionType;
	uint8_t		padding_0[ 7 ];
} platformInfo_DEPRECATED_t;

typedef struct _platformData_t {
	uint64_t						checksum;
	struct pluginInterface_type		pluginInterface;
	struct platformInterface_type	platformInterface;
	struct systemInterface_type *	systemInterface;
	struct platformInfo_type		platformInfo;
	struct buildInfo_type			buildInfo;
	char							mapName[ MAX_MAP_NAME_LENGTH ];
	struct processorInfo_type		processorInfo[ MAX_PROCESSORS ];
	size_t							processorInfoCount;
	struct graphicsInfo_type		graphicsInfo[ MAX_GRAPHICSCARDS ];
	size_t							graphicsInfoCount;
	callbackInfo_t					onPlatformInfoCB[ MAX_CALLBACKS ];
	size_t							onPlatformInfoCount;
	callbackInfo_t					onGraphicsInfoCB[ MAX_CALLBACKS ];
	size_t							onGraphicsInfoCount;
	callbackInfo_t					onBuildInfoCB[ MAX_CALLBACKS ];
	size_t							onBuildInfoCount;
	callbackInfo_t					onMapLoadCB[ MAX_CALLBACKS ];
	size_t							onMapLoadCount;
	callbackInfo_t					onProcessorInfoCB[ MAX_CALLBACKS ];
	size_t							onProcessorInfoCount;
} platformData_t;

/*
========================
pluginInterfaceToMe
========================
*/
static platformData_t* pluginInterfaceToMe( struct pluginInterface_type * const iface ) {
	if ( iface != NULL ) {
		const uint64_t * const checksum = ( ( uint64_t* )iface ) - 1;
		if ( *checksum == PLUGIN_PLATFORM_CHECKSUM ) {
			return ( platformData_t* )checksum;
		}
	}
	return NULL;
}

/*
========================
platformInterfaceToMe
========================
*/
static platformData_t* platformInterfaceToMe( struct platformInterface_type * const iface ) {
	const uint8_t * const ptrToPlatformInterface = ( uint8_t* )iface;
	const uint8_t * const ptrToPluginInterface = ptrToPlatformInterface - sizeof( struct pluginInterface_type );
	const uint8_t * const ptrToChecksum = ptrToPluginInterface - sizeof( uint64_t );
	if ( *( uint64_t* )ptrToChecksum == PLUGIN_PLATFORM_CHECKSUM ) {
		return ( platformData_t* )ptrToChecksum;
	}
	return NULL;
}

/*
========================
registerEventHandler
========================
*/
static void registerEventHandler(	callbackInfo_t * const list, 
									size_t * const count,
									genericCB_t cb,
									void * const param ) {
	if ( *count == MAX_CALLBACKS ) {
		size_t i;
		for ( i = 0; i < MAX_CALLBACKS; ++i ) {
			callbackInfo_t * const info = list + i;
			if ( info->cb == 0 ) {
				info->cb = cb;
				info->param = param;
				return;
			}
		}
	} else {
		callbackInfo_t * const info = list + *count;
		info->cb = cb;
		info->param = param;
		*count += 1;
	}
}

/*
========================
unregisterOnBeginFrame
========================
*/
static void unregisterEventHandler(	callbackInfo_t * const list,
									const size_t count,
									genericCB_t cb,
									void * const param ) {
	size_t i;
	for ( i = 0; i < count; ++i ) {
		callbackInfo_t * const info = list + i;
		if ( info->cb == cb && info->param == param ) {
			info->cb = 0;
			info->param = 0;
		}
	}
}

/*
========================
registerOnPlatformInfo
========================
*/
static void registerOnPlatformInfo(	struct platformInterface_type * const iface,
									onPlatformInfoCallback_t cb,
									void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onPlatformInfoCB, &me->onPlatformInfoCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnPlatformInfo
========================
*/
static void unregisterOnPlatformInfo(	struct platformInterface_type * const iface, 
										onPlatformInfoCallback_t cb,
										void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onPlatformInfoCB, me->onPlatformInfoCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnGraphicsInfo
========================
*/
static void registerOnGraphicsInfo(	struct platformInterface_type * const iface, 
									onGraphicsInfoCallback_t cb,
									void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onGraphicsInfoCB, &me->onGraphicsInfoCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnGraphicsInfo
========================
*/
static void unregisterOnGraphicsInfo(	struct platformInterface_type * const iface, 
										onGraphicsInfoCallback_t cb,
										void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onGraphicsInfoCB, me->onGraphicsInfoCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnBuildInfo
========================
*/
static void registerOnBuildInfo(	struct platformInterface_type * const iface, 
									onBuildInfoCallback_t cb,
									void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onBuildInfoCB, &me->onBuildInfoCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnBuildInfo
========================
*/
static void unregisterOnBuildInfo(	struct platformInterface_type * const iface, 
									onBuildInfoCallback_t cb,
									void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onBuildInfoCB, me->onBuildInfoCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnMapLoad
========================
*/
static void registerOnMapLoad(	struct platformInterface_type * const iface, 
								onMapLoadCallback_t cb,
								void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onMapLoadCB, &me->onMapLoadCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnMapLoad
========================
*/
static void unregisterOnMapLoad(	struct platformInterface_type * const iface, 
									onMapLoadCallback_t cb,
									void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onMapLoadCB, me->onMapLoadCount, ( genericCB_t )cb, param );
}

/*
========================
registerOnProcessorInfo
========================
*/
static void registerOnProcessorInfo(	struct platformInterface_type * const iface, 
										onProcessorInfoCallback_t cb,
										void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	registerEventHandler( me->onProcessorInfoCB, &me->onProcessorInfoCount, ( genericCB_t )cb, param );
}

/*
========================
unregisterOnProcessorInfo
========================
*/
static void unregisterOnProcessorInfo(	struct platformInterface_type * const iface, 
										onProcessorInfoCallback_t cb,
										void * const param ) {
	platformData_t * const me = platformInterfaceToMe( iface );
	if ( me == NULL ) {
		return;
	}
	unregisterEventHandler( me->onProcessorInfoCB, me->onProcessorInfoCount, ( genericCB_t )cb, param );
}

/*
========================
onPlatformInfo
========================
*/
static void onPlatformInfo( void * const param, const struct packetHeader_type * const header ) {
	platformData_t * const me = ( platformData_t* )param;
	size_t i;

	memcpy( &me->platformInfo, header + 1, sizeof( struct platformInfo_type ) );

	for ( i = 0; i < me->onPlatformInfoCount; ++i ) {
		callbackInfo_t * const info = me->onPlatformInfoCB + i;
		if ( info->cb ) {
			( ( onPlatformInfoCallback_t )info->cb )( info->param, &me->platformInfo );
		}
	}
}

/*
========================
onBuildInfo
========================
*/
static void onBuildInfo( void * const param, const struct packetHeader_type * const header ) {
	platformData_t * const me = ( platformData_t* )param;
	size_t i;

	memcpy( &me->buildInfo, header + 1, sizeof( struct buildInfo_type ) );

	for ( i = 0; i < me->onBuildInfoCount; ++i ) {
		callbackInfo_t * const info = me->onBuildInfoCB + i;
		if ( info->cb ) {
			( ( onBuildInfoCallback_t )info->cb )( info->param, &me->buildInfo );
		}
	}
}

/*
========================
onMapLoad
========================
*/
static void onMapLoad( void * const param, const struct packetHeader_type * const header ) {
	platformData_t * const me = ( platformData_t* )param;
	const size_t length = ( size_t )header->size - sizeof( struct packetHeader_type );
	size_t i;

	strncpy( me->mapName, ( const char* )( header + 1 ), sizeof( me->mapName ) );
	me->mapName[ min( length, sizeof( me->mapName ) - 1 ) ] = 0;

	for ( i = 0; i < me->onMapLoadCount; ++i ) {
		callbackInfo_t * const info = me->onMapLoadCB + i;
		if ( info->cb ) {
			( ( onMapLoadCallback_t )info->cb )( info->param, me->mapName );
		}
	}
}

/*
========================
onProcessorInfo
========================
*/
static void onProcessorInfo( void * const param, const struct packetHeader_type * const header ) {
	typedef struct _procPacket_t {
		struct packetHeader_type hdr;
		uint16_t manufacturer;
		uint16_t name;
		uint16_t clockSpeedMHz;
		uint16_t l1CacheSizeKB;
		uint16_t l2CacheSizeKB;
		uint16_t l3CacheSizeKB;
		uint8_t numPhysicalCores;
		uint8_t numLogicalCores;
		uint8_t architecture;
		uint8_t addressWidth;
	} procPacket_t;

	platformData_t * const me = ( platformData_t* )param;
	size_t i;

	procPacket_t * const pkt = ( procPacket_t* )header;
	const uint32_t index = me->processorInfoCount & ( MAX_PROCESSORS - 1 );
	struct processorInfo_type * const data = me->processorInfo + index;

	memset( data, 0, sizeof( struct processorInfo_type ) );

	data->manufacturer = me->systemInterface->findStringByID( me->systemInterface, pkt->manufacturer );
	data->name = me->systemInterface->findStringByID( me->systemInterface, pkt->name );
	data->clockSpeedMHz = pkt->clockSpeedMHz;
	data->l1CacheSizeKB = pkt->l1CacheSizeKB;
	data->l2CacheSizeKB = pkt->l2CacheSizeKB;
	data->l3CacheSizeKB = pkt->l3CacheSizeKB;
	data->numPhysicalCores = pkt->numPhysicalCores;
	data->numLogicalCores = pkt->numLogicalCores;
	data->architecture = pkt->architecture;
	data->addressWidth = pkt->addressWidth;

	for ( i = 0; i < me->onProcessorInfoCount; ++i ) {
		callbackInfo_t * const info = me->onProcessorInfoCB + i;
		if ( info->cb ) {
			( ( onProcessorInfoCallback_t )info->cb )( info->param, data );
		}
	}
}

/*
========================
onGraphicsInfo
========================
*/
static void onGraphicsInfo( void * const param, const struct packetHeader_type * const header ) {
	typedef struct _gfxPacket_t {
		struct packetHeader_type hdr;
		uint16_t vendor;
		uint16_t name;
		uint16_t driverVersion;
		uint16_t memoryDedicatedMB;
		uint16_t memoryTotalMB;
		uint8_t padding[ 6 ];
	} gfxPacket_t;

	platformData_t * const me = ( platformData_t* )param;
	size_t i;

	gfxPacket_t * const pkt = ( gfxPacket_t* )header;
	const size_t index = me->graphicsInfoCount & ( MAX_GRAPHICSCARDS - 1 );
	struct graphicsInfo_type * const data = me->graphicsInfo + index;

	memset( data, 0, sizeof( struct graphicsInfo_type ) );

	data->vendor = me->systemInterface->findStringByID( me->systemInterface, pkt->vendor );
	data->name = me->systemInterface->findStringByID( me->systemInterface, pkt->name );
	data->driverVersion = me->systemInterface->findStringByID( me->systemInterface, pkt->driverVersion );
	data->memoryDedicatedMB = pkt->memoryDedicatedMB;
	data->memoryTotalMB = pkt->memoryTotalMB;

	for ( i = 0; i < me->onGraphicsInfoCount; ++i ) {
		callbackInfo_t * const info = me->onGraphicsInfoCB + i;
		if ( info->cb ) {
			( ( onGraphicsInfoCallback_t )info->cb )( info->param, data );
		}
	}
}

/*
========================
onGraphicsInfo_DEPRECATED
========================
*/
static void onGraphicsInfo_DEPRECATED( void * const param, const struct packetHeader_type * const header ) {
	typedef struct _graphicsInfo_DEPRECATED_t {
		char vendor[ 32 ];
		char driverDesc[ 48 ];
		char driverVersion[ 32 ];
	} graphicsInfo_DEPRECATED_t;

	platformData_t * const me = ( platformData_t* )param;
	size_t i;

	graphicsInfo_DEPRECATED_t * const pkt = ( graphicsInfo_DEPRECATED_t* )header;
	const uint32_t index = me->graphicsInfoCount & ( MAX_GRAPHICSCARDS - 1 );
	struct graphicsInfo_type * const data = me->graphicsInfo + index;

	memset( data, 0, sizeof( struct graphicsInfo_type ) );

	data->vendor = me->systemInterface->addString( me->systemInterface, pkt->vendor );
	data->name = me->systemInterface->addString( me->systemInterface, pkt->driverDesc );
	data->driverVersion = me->systemInterface->addString( me->systemInterface, pkt->driverVersion );

	for ( i = 0; i < me->onGraphicsInfoCount; ++i ) {
		callbackInfo_t * const info = me->onGraphicsInfoCB + i;
		if ( info->cb ) {
			( ( onGraphicsInfoCallback_t )info->cb )( info->param, data );
		}
	}
}

/*
========================
myStart
========================
*/
static void myStart( struct pluginInterface_type * const self ) {
	platformData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_PLATFORM,			onPlatformInfo,				me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_BUILDINFO,			onBuildInfo,				me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_MAPLOAD,				onMapLoad,					me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_PROCESSOR,			onProcessorInfo,			me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_GRAPHICS,			onGraphicsInfo,				me );
	me->systemInterface->registerForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_GRAPHICS_DEPRECATED,	onGraphicsInfo_DEPRECATED,	me );
}

/*
========================
myStop
========================
*/
static void myStop( struct pluginInterface_type * const self ) {
	platformData_t * const me = pluginInterfaceToMe( self );

	if ( me == NULL ) {
		return;
	}

	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_GRAPHICS_DEPRECATED,	onGraphicsInfo_DEPRECATED,	me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_GRAPHICS,				onGraphicsInfo,				me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_PROCESSOR,				onProcessorInfo,			me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_MAPLOAD,				onMapLoad,					me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_BUILDINFO,				onBuildInfo,				me );
	me->systemInterface->unregisterForPacket( me->systemInterface, SYSTEM_SYSTEM, PACKET_PLATFORM,				onPlatformInfo,				me );
}

/*
========================
myGetName
========================
*/
static const char* myGetName( struct pluginInterface_type * const self ) {
	( void )self;
	return PLUGIN_NAME_PLATFORM;
}

/*
========================
PlatformGetPrivateInterface
========================
*/
static void* myGetPrivateInterface( struct pluginInterface_type * const self ) {
	platformData_t * const me = pluginInterfaceToMe( self );
	if ( me == NULL ) {
		return NULL;
	}
	return &me->platformInterface;
}

/*
========================
Platform_Create
========================
*/
struct pluginInterface_type * Platform_Create( struct systemInterface_type * const sys ) {
	platformData_t * const me = sys->allocate( sys, sizeof( platformData_t ) );

	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( platformData_t ) );

	me->checksum = PLUGIN_PLATFORM_CHECKSUM;

	me->pluginInterface.start				= myStart;
	me->pluginInterface.stop				= myStop;
	me->pluginInterface.getName				= myGetName;
	me->pluginInterface.getPrivateInterface	= myGetPrivateInterface;

	me->platformInterface.registerOnPlatformInfo	= registerOnPlatformInfo;
	me->platformInterface.unregisterOnPlatformInfo	= unregisterOnPlatformInfo;
	me->platformInterface.registerOnGraphicsInfo	= registerOnGraphicsInfo;
	me->platformInterface.unregisterOnGraphicsInfo	= unregisterOnGraphicsInfo;
	me->platformInterface.registerOnBuildInfo		= registerOnBuildInfo;
	me->platformInterface.unregisterOnBuildInfo		= unregisterOnBuildInfo;
	me->platformInterface.registerOnMapLoad			= registerOnMapLoad;
	me->platformInterface.unregisterOnMapLoad		= unregisterOnMapLoad;
	me->platformInterface.registerOnProcessorInfo	= registerOnProcessorInfo;
	me->platformInterface.unregisterOnProcessorInfo	= unregisterOnProcessorInfo;

	me->systemInterface = sys;

	return &me->pluginInterface;
}
