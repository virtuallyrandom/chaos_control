/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2015 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#ifndef __SYMBOL_PDB_H__
#define __SYMBOL_PDB_H__

typedef struct _symbolPdbData_t* symbolPdb_t;

typedef struct _symbolInfo_t {
	uintptr_t	addr;
	uint64_t	size;
	uint32_t	line;
	uint32_t	column;
	uint32_t	fileUID;
	uint32_t	functionUID;
} symbolInfo_t;

typedef struct _symbolString_t {
	uint32_t	index;
	uint32_t	uid;
	char*		value;
} symbolString_t;

#if defined( __cplusplus )
extern "C" {
#endif /* defined( __cplusplus ) */

symbolPdb_t	SymbolPdb_Create( void );
void		SymbolPdb_Destroy( symbolPdb_t const me );

int32_t		SymbolPdb_Load( symbolPdb_t const me, const char * const filename );

size_t		SymbolPdb_EnumerateFiles(	symbolPdb_t const me,
										int32_t ( *cb )( void * const, const symbolString_t * const ),
										void * const param );

size_t		SymbolPdb_EnumerateFunctions(	symbolPdb_t const me,
											int32_t ( *cb )( void * const, const symbolString_t * const ),
											void * const param );

size_t		SymbolPdb_EnumerateSymbols(	symbolPdb_t const me,
											int32_t ( *cb )( void * const, const symbolInfo_t * const ),
											void * const param );

#if defined( __cplusplus )
}
#endif /* defined( __cplusplus ) */

#endif /* __SYMBOL_PDB_H__ */
