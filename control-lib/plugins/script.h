/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2016 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __SCRIPT_H__
#define __SCRIPT_H__

struct systemInterface_type;
struct pluginInterface_type;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pluginInterface_type * Script_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __SCRIPT_H__ */
