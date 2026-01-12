/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __PLUGIN_THREAD_H__
#define __PLUGIN_THREAD_H__

struct systemInterface_type;
struct pluginInterface_type;

typedef struct _threadInterface_t {
	const char * ( * getThreadName )( struct _threadInterface_t * const, const uint64_t id );
} threadInterface_t;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_THREAD[];
struct pluginInterface_type * Thread_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __PLUGIN_THREAD_H__ */
