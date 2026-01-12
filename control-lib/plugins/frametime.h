/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __FRAMETIME_H__
#define __FRAMETIME_H__

#define MAX_FRAME_TIME_ENTRIES 64

struct systemInterface_type;
struct pluginInterface_type;

typedef struct _timeInfo_t {
	const char *name;
	uint8_t id;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint32_t thresholdTimeUS;
	uint32_t maxPassTimeUS;
	uint32_t minFailTimeUS;
} timeInfo_t;

typedef struct _frameTime_t {
	uint32_t count;
	uint32_t entry[ MAX_FRAME_TIME_ENTRIES ];
} frameTime_t;

typedef void ( *onFrametimeEventCallback	)( void * const param, const uint64_t time );
typedef void ( *onFrameTimeInfoCallback		)( void * const param, const timeInfo_t * const info );
typedef void ( *onFrametimeTimeCallback		)( void * const param, const frameTime_t * const time );

struct frametimeInterface_type {
	void ( *registerOnBeginGameFrame		)( struct frametimeInterface_type * const, onFrametimeEventCallback, void * const param );
	void ( *unregisterOnBeginGameFrame		)( struct frametimeInterface_type * const, onFrametimeEventCallback, void * const param );
	void ( *registerOnEndGameFrame			)( struct frametimeInterface_type * const, onFrametimeEventCallback, void * const param );
	void ( *unregisterOnEndGameFrame		)( struct frametimeInterface_type * const, onFrametimeEventCallback, void * const param );
	void ( *registerOnBeginRenderFrame		)( struct frametimeInterface_type * const, onFrametimeEventCallback, void * const param );
	void ( *unregisterOnBeginRenderFrame	)( struct frametimeInterface_type * const, onFrametimeEventCallback, void * const param );
	void ( *registerOnEndRenderFrame		)( struct frametimeInterface_type * const, onFrametimeEventCallback, void * const param );
	void ( *unregisterOnEndRenderFrame		)( struct frametimeInterface_type * const, onFrametimeEventCallback, void * const param );
	void ( *registerOnPulse					)( struct frametimeInterface_type * const, onFrametimeTimeCallback, void * const param );
	void ( *unregisterOnPulse				)( struct frametimeInterface_type * const, onFrametimeTimeCallback, void * const param );
	void ( *registerOnMain					)( struct frametimeInterface_type * const, onFrametimeTimeCallback, void * const param );
	void ( *unregisterOnMain				)( struct frametimeInterface_type * const, onFrametimeTimeCallback, void * const param );
	void ( *registerOnRenderTime			)( struct frametimeInterface_type * const, onFrametimeTimeCallback, void * const param );
	void ( *unregisterOnRenderTime			)( struct frametimeInterface_type * const, onFrametimeTimeCallback, void * const param );
	void ( *registerOnGPUTime				)( struct frametimeInterface_type * const, onFrametimeTimeCallback, void * const param );
	void ( *unregisterOnGPUTime				)( struct frametimeInterface_type * const, onFrametimeTimeCallback, void * const param );

	const timeInfo_t* ( *getPulseTimeInfo	)( struct frametimeInterface_type * const, size_t * const optionalCount );
	const timeInfo_t* ( *getMainTimeInfo	)( struct frametimeInterface_type * const, size_t * const optionalCount );
	const timeInfo_t* ( *getRenderTimeInfo	)( struct frametimeInterface_type * const, size_t * const optionalCount );
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_FRAMETIME[];
struct pluginInterface_type * Frametime_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __FRAMETIME_H__ */
