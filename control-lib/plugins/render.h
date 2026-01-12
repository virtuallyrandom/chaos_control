/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __RENDER_H__
#define __RENDER_H__

struct systemInterface_type;
struct pluginInterface_type;

typedef void ( * onResolutionScalingCallback )(	void * const param,
												const float x,
												const float y,
												const uint64_t time );

typedef void ( * onDimensionsCallback )(	void * const param, const int32_t x, const int32_t y );

struct renderInterface_type {
	void ( * registerOnResolutionScaling )( struct renderInterface_type * const, onResolutionScalingCallback, void * const param );
	void ( * unregisterOnResolutionScaling )( struct renderInterface_type * const, onResolutionScalingCallback, void * const param );
	void ( * registerOnDimensions )( struct renderInterface_type * const, onDimensionsCallback, void * const param );
	void ( * unregisterOnDimensions )( struct renderInterface_type * const, onDimensionsCallback, void * const param );
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_RENDER[];
struct pluginInterface_type * Render_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __RENDER_H__ */
