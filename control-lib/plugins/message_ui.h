/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __MESSAGE_UI_H__
#define __MESSAGE_UI_H__

struct systemInterface_type;
struct pluginInterface_type;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pluginInterface_type * MessageUI_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __MESSAGE_UI_H__ */
