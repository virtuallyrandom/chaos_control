/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __SYMBOL_H__
#define __SYMBOL_H__

struct systemInterface_type;
struct pluginInterface_type;

typedef struct _symbolInterface_t {
	int ( *find )(	struct _symbolInterface_t * const,
					const uint64_t addr,
					const char ** const name,
					const char ** const file,
					uint32_t * const line );
} symbolInterface_t;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const char PLUGIN_NAME_SYMBOL[];
struct pluginInterface_type * Symbol_Create( struct systemInterface_type * const );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __SYMBOL_H__ */
