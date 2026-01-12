/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __MESSAGE_H__
#define __MESSAGE_H__

struct systemInterface_type;
struct pluginInterface_type;

typedef void ( *onMessageCallback )( void * const param, const char * const message, const size_t length );

struct messageInterface_type {
	void ( *registerOnMessage )( struct messageInterface_type * const, onMessageCallback, void * const param );
	void ( *unregisterOnMessage )( struct messageInterface_type * const, onMessageCallback, void * const param );
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_MESSAGE[];
struct pluginInterface_type * Message_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __MESSAGE_H__ */
