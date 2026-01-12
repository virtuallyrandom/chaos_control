/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __PLATFORM_H__
#define __PLATFORM_H__

struct systemInterface_type;
struct pluginInterface_type;

struct platformInfo_type {
	uint64_t	clockTicksPerSecond;
	struct {
		uint32_t year	: 6; /* [0..63], add 2000 for year */
		uint32_t month	: 4; /* [0..15], add 1, 1 = January */
		uint32_t day	: 5; /* [0..31] */
		uint32_t hour	: 5; /* [0..31], add 1 */
		uint32_t minute	: 6; /* [0..63] */
		uint32_t second	: 6; /* [0..63] */
	} timeInfo; /* 32b */
	uint8_t		platform;
	uint8_t		binaryType;
	uint8_t		renderType;
	uint8_t		productionType;
};

struct graphicsInfo_type {
	const char *vendor;
	const char *name;
	const char *driverVersion;
	uint16_t memoryDedicatedMB;
	uint16_t memoryTotalMB;
	uint32_t padding;
};

struct processorInfo_type {
	const char *manufacturer;
	const char *name;
	uint16_t clockSpeedMHz;
	uint16_t l1CacheSizeKB;
	uint16_t l2CacheSizeKB;
	uint16_t l3CacheSizeKB;
	uint8_t numPhysicalCores;
	uint8_t numLogicalCores;
	uint8_t architecture;
	uint8_t addressWidth;
	uint32_t padding;
};

struct buildInfo_type {
	char binaryDesc[ 32 ];
	char packageDesc[ 48 ];
	char componentDesc[ 32 ];
};

typedef void ( *onPlatformInfoCallback_t )( void * const param, const struct platformInfo_type * const );
typedef void ( *onGraphicsInfoCallback_t )( void * const param, const struct graphicsInfo_type * const );
typedef void ( *onBuildInfoCallback_t )( void * const param, const struct buildInfo_type * const );
typedef void ( *onMapLoadCallback_t )( void * const param, const char * const mapName );
typedef void ( *onProcessorInfoCallback_t )( void * const param, const struct processorInfo_type * const );

struct platformInterface_type {
	void ( *registerOnPlatformInfo		)( struct platformInterface_type * const, onPlatformInfoCallback_t, void * const param );
	void ( *unregisterOnPlatformInfo	)( struct platformInterface_type * const, onPlatformInfoCallback_t, void * const param );
	void ( *registerOnGraphicsInfo		)( struct platformInterface_type * const, onGraphicsInfoCallback_t, void * const param );
	void ( *unregisterOnGraphicsInfo	)( struct platformInterface_type * const, onGraphicsInfoCallback_t, void * const param );
	void ( *registerOnBuildInfo			)( struct platformInterface_type * const, onBuildInfoCallback_t, void * const param );
	void ( *unregisterOnBuildInfo		)( struct platformInterface_type * const, onBuildInfoCallback_t, void * const param );
	void ( *registerOnMapLoad			)( struct platformInterface_type * const, onMapLoadCallback_t, void * const param );
	void ( *unregisterOnMapLoad			)( struct platformInterface_type * const, onMapLoadCallback_t, void * const param );
	void ( *registerOnProcessorInfo		)( struct platformInterface_type * const, onProcessorInfoCallback_t, void * const param );
	void ( *unregisterOnProcessorInfo	)( struct platformInterface_type * const, onProcessorInfoCallback_t, void * const param );
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_PLATFORM[];
struct pluginInterface_type * Platform_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PLATFORM_H__ */
