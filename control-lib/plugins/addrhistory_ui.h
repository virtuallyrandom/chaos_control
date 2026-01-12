/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __ADDRHISTORY_UI_H__
#define __ADDRHISTORY_UI_H__

struct systemInterface_type;
struct pluginInterface_type;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct pluginInterface_type * AddrHistoryUI_Create( struct systemInterface_type * const sys );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __ADDRHISTORY_UI_H__ */
