/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __PROFILE_H__
#define __PROFILE_H__

struct systemInterface_type;
struct pluginInterface_type;

typedef void ( *onProfileSyncCallback )( void *param, const uint64_t time );

typedef void ( *onProfileEnterCallback )(	void * const param,
											const uint64_t threadID,
											const uint64_t time,
											const uint64_t groupMask,
											const char * label );

typedef void ( *onProfileLeaveCallback )(	void * const param,
											const uint64_t threadID,
											const uint64_t time );

struct profileInterface_type {
	void ( *registerOnSync )( struct profileInterface_type * const, onProfileSyncCallback, void * const param );
	void ( *unregisterOnSync )( struct profileInterface_type * const, onProfileSyncCallback, void * const param );
	void ( *registerOnEnter )( struct profileInterface_type * const, onProfileEnterCallback, void * const param );
	void ( *unregisterOnEnter )( struct profileInterface_type * const, onProfileEnterCallback, void * const param );
	void ( *registerOnLeave )( struct profileInterface_type * const, onProfileLeaveCallback, void * const param );
	void ( *unregisterOnLeave )( struct profileInterface_type * const, onProfileLeaveCallback, void * const param );
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_PROFILE[];
struct pluginInterface_type * Profile_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PROFILE_H__ */
