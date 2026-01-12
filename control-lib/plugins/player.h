/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __PLAYER_H__
#define __PLAYER_H__

struct systemInterface_type;
struct pluginInterface_type;

struct playerPosition_type {
	uint64_t	time;
	uint32_t	playerID;
	float		yaw;
	float		pitch;
	float		roll;
	float		position[ 3 ];
	uint32_t	padding;
};

typedef void ( * onPlayerPositionCallback_t )( void * const param, const struct playerPosition_type * const );

struct playerInterface_type {
	void ( * registerOnPlayerPosition	)( struct playerInterface_type * const, onPlayerPositionCallback_t, void * const param );
	void ( * unregisterOnPlayerPosition	)( struct playerInterface_type * const, onPlayerPositionCallback_t, void * const param );
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_PLAYER[];
struct pluginInterface_type * Player_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PLAYER_H__ */
