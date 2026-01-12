/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include "symbol_pdb.h"

#pragma warning( push, 1 )
#include <dia2.h>
#pragma warning( pop )

#pragma comment( lib, "diaguids.lib" )

#if !defined( UNDNAME_NAME_ONLY )
static const int UNDNAME_NAME_ONLY = 0x1000;
#endif /* !defined( UNDNAME_NAME_ONLY ) */

typedef struct _symbolPdbData_t {
	IDiaDataSource *source;
	IDiaSession *session;
} symbolPdbData_t;

#if defined( _DEBUG )
#define VERIFY( result, test ) ( ( result ) == ( test ) ? 1 : __debugbreak(), 0 )
#else /* !defined( _DEBUG ) */
#define VERIFY( result, test ) ( ( result ) == ( test ) ? 1 : 0 )
#endif /* !defined( _DEBUG ) */

/*
========================
SymbolPdb_Create
========================
*/
extern "C" symbolPdb_t SymbolPdb_Create() {
	HRESULT hr;
	symbolPdb_t me;
	
	hr = CoInitialize( NULL );
	if ( FAILED( hr ) ) {
		return NULL;
	}

	me = ( symbolPdb_t )malloc( sizeof( symbolPdbData_t ) );
	if ( me == NULL ) {
		return NULL;
	}

	memset( me, 0, sizeof( symbolPdbData_t ) );

	hr = CoCreateInstance(	__uuidof( DiaSource ),
							NULL,
							CLSCTX_INPROC_SERVER,
							__uuidof( IDiaDataSource ),
							( void** )&me->source );
	if ( FAILED( hr ) ) {
		free( me );
		return NULL;
	}

	return me;
}

/*
========================
SymbolPdb_Destroy
========================
*/
extern "C" void SymbolPdb_Destroy( symbolPdb_t const me ) {
	if ( me == NULL ) {
		return;
	}

	if ( me->session ) {
		VERIFY( 0, me->session->Release() );
		me->session = NULL;
	}

	if ( me->source ) {
		VERIFY( 0, me->source->Release() );
		me->source = NULL;
	}

	free( me );

	CoUninitialize();
}

/*
========================
SymbolPdb_Load
========================
*/
extern "C" int32_t SymbolPdb_Load( symbolPdb_t const me, const char * const filename ) {
	( void )me;
	( void )filename;
	wchar_t wfile[ MAX_PATH ];
	HRESULT hr;

	if ( me == NULL ) {
		return 0;
	}

	if ( me->source == NULL ) {
		return 0;
	}

	mbstowcs( wfile, filename, MAX_PATH );

	hr = me->source->loadDataFromPdb( wfile );
	if ( FAILED( hr ) ) {
		return 0;
	}

	hr = me->source->openSession( &me->session );
	if ( FAILED( hr ) ) {
		return 0;
	}

	return 1;
}

/*
========================
SymbolPdb_EnumerateFiles
========================
*/
size_t SymbolPdb_EnumerateFiles(	symbolPdb_t const me,
									int32_t ( *cb )( void * const, const symbolString_t * const ),
									void * const param ) {
	HRESULT hr;
	IDiaEnumSourceFiles *enumFiles;
	char path[ MAX_PATH ];
	symbolString_t info;
	LONG count = 0;

	if ( me == NULL ) {
		return 0;
	}

	if ( me->session == NULL ) {
		return 0;
	}

	hr = me->session->findFile( NULL, NULL, nsNone, &enumFiles );
	if ( FAILED( hr ) ) {
		return 0;
	}

	info.index = 0;
	info.value = path;

	enumFiles->get_Count( &count );

	for ( ;; ) {
		IDiaSourceFile *sourceFile = 0;
		BSTR name;
		ULONG fetched;

		hr = enumFiles->Next( 1, &sourceFile, &fetched );
		if ( FAILED( hr ) || sourceFile == NULL || fetched != 1 ) {
			break;
		}

		hr = sourceFile->get_fileName( &name );
		if ( FAILED( hr ) ) {
			VERIFY( 0, sourceFile->Release() );
			continue;
		}

		hr = sourceFile->get_uniqueId( ( DWORD* )&info.uid );
		if ( FAILED( hr ) ) {
			VERIFY( 0, sourceFile->Release() );
			continue;
		}

		wcstombs( path, name, MAX_PATH );
		path[ MAX_PATH - 1 ] = 0;

		cb( param, &info );

		info.index++;

		SysFreeString( name );

		VERIFY( 0, sourceFile->Release() );
	}

	VERIFY( 0, enumFiles->Release() );

	return ( size_t )count;
}

/*
========================
SymbolPdb_EnumerateFunctions
========================
*/
size_t SymbolPdb_EnumerateFunctions(	symbolPdb_t const me,
										int32_t ( *cb )( void * const, const symbolString_t * const ),
										void * const param ) {
	HRESULT hr;
	IDiaSymbol *symRoot;
	IDiaEnumSymbols *symEnum;
	ULONG fetched;
	uint32_t count = 0;
	symbolString_t info;
	char infoTemp[ 1024 ];

	if ( me == NULL ) {
		return 0;
	}

	hr = me->session->get_globalScope( &symRoot );
	if ( FAILED( hr ) ) {
		return 0;
	}

	info.index = 0;
	info.uid = 0;
	info.value = infoTemp;

	hr = symRoot->findChildren( SymTagFunction, NULL, NULL, &symEnum );
	if ( SUCCEEDED( hr ) ) {
		ULONG remaining = 0;
		size_t i;
		int32_t go = 1;
		IDiaSymbol** sym;

		symEnum->get_Count( ( LONG* )&remaining );

		sym = ( IDiaSymbol** )malloc( sizeof( IDiaSymbol* ) * ( size_t )remaining );
		if ( sym == NULL ) {
			VERIFY( 0, symEnum->Release() );
			VERIFY( 0, symRoot->Release() );
			return 0;
		}

		while ( go && remaining > 0 ) {
			hr = symEnum->Next( remaining, sym, &fetched );
			if ( FAILED( hr ) || fetched == 0 ) {
				break;
			}

			remaining -= fetched;
			count += fetched;

			for ( i = 0; go && i < ( size_t )fetched; i++ ) {
				BSTR symbolName = NULL;

				/* this call is bloody slow... */
				sym[ i ]->get_undecoratedNameEx( UNDNAME_NAME_ONLY, &symbolName );
				sym[ i ]->get_symIndexId( ( DWORD* )&info.uid );

				if ( symbolName != NULL ) {
					wcstombs( infoTemp, symbolName, sizeof( infoTemp ) / sizeof( *infoTemp ) );
					SysFreeString( symbolName );
				} else {
					strcpy( infoTemp, "unknown" );
				}

				infoTemp[ sizeof( infoTemp ) / sizeof( *infoTemp ) - 1 ] = 0;

				go = cb( param, &info );

				info.index++;

				VERIFY( 0, sym[ i ]->Release() );
			}

			/* in case the load is canceled, ensure cleanup */
			for ( ; i < ( size_t ) fetched; i++ ) {
				VERIFY( 0, sym[ i ]->Release() );
			}
		}

		free( sym );

		VERIFY( 0, symEnum->Release() );
	}

	VERIFY( 0, symRoot->Release() );

	return ( size_t )count;
}

/*
========================
SymbolPdb_Symbols
========================
*/
size_t SymbolPdb_EnumerateSymbols(	symbolPdb_t const me,
									int32_t ( *cb )( void * const, const symbolInfo_t * const ),
									void * const param ) {
	HRESULT hr;
	IDiaEnumSourceFiles *enumFiles = 0;
	IDiaSymbol *symRoot = 0;
	symbolInfo_t info;
	LONG count = 0;
	int32_t go = 1;

	IDiaSourceFile**	file = 0;
	size_t				fileCount = 0;

	IDiaEnumSymbols *enumCompilands = 0;
	ULONG remaining;
	size_t i;

	if ( me == NULL ) {
		return 0;
	}

	if ( me->session == NULL ) {
		return 0;
	}

	hr = me->session->get_globalScope( &symRoot );
	if ( FAILED( hr ) || symRoot == 0 ) {
		return 0;
	}

	hr = me->session->findFile( NULL, NULL, nsNone, &enumFiles );
	if ( FAILED( hr ) || enumFiles == 0 ) {
		VERIFY( 0, symRoot->Release() );
		return 0;
	}

	enumFiles->get_Count( ( LONG* )&remaining );

	if ( remaining > fileCount ) {
		fileCount = ( size_t )remaining;
		if ( file ) {
			free( file );
		}
		file = ( IDiaSourceFile** )malloc( sizeof( IDiaSourceFile* ) * fileCount );
	}

	if ( file == NULL ) {
		VERIFY( 0, enumFiles->Release() );
		VERIFY( 0, symRoot->Release() );
		return 0;
	}

	while ( go && remaining ) {
		ULONG fetchedFiles;

		hr = enumFiles->Next( remaining, file, &fetchedFiles );
		if ( FAILED( hr ) || fetchedFiles == 0 ) {
			break;
		}

		remaining -= fetchedFiles;

		for ( i = 0; i < ( size_t )fetchedFiles && go; i++ ) {
			hr = file[ i ]->get_compilands( &enumCompilands );
			if ( FAILED( hr ) || enumCompilands == NULL ) {
				VERIFY( 0, file[ i ]->Release() );
				continue;
			}

			hr = file[ i ]->get_uniqueId( ( DWORD* )&info.fileUID );
			if ( FAILED( hr ) ) {
				VERIFY( 0, enumCompilands->Release() );
				VERIFY( 0, file[ i ]->Release() );
				continue;
			}

			while ( go ) {
				IDiaSymbol *compiland = 0;
				IDiaEnumLineNumbers *enumLineNum = 0;
				ULONG fetchedCompilands;

				hr = enumCompilands->Next( 1, &compiland, &fetchedCompilands );
				if ( FAILED( hr ) || fetchedCompilands == 0 ) {
					break;
				}

				hr = me->session->findLines( compiland, file[ i ], &enumLineNum );
				if ( FAILED( hr ) || enumLineNum == NULL ) {
					VERIFY( 0, compiland->Release() );
					continue;
				}

				while ( go ) {
					IDiaLineNumber *line = 0;
					IDiaSymbol *func = 0;
					ULONG fetchedLines;

					hr = enumLineNum->Next( 1, &line, &fetchedLines );
					if ( FAILED( hr ) || line == NULL ) {
						break;
					}

					VERIFY( S_OK, line->get_virtualAddress( ( ULONGLONG* )&info.addr ) );
					VERIFY( S_OK, line->get_lineNumber( ( DWORD* )&info.line ) );
					VERIFY( S_OK, line->get_columnNumber( ( DWORD* )&info.column ) );

					/* todo: use the addr to get the function and size */
					hr = me->session->findSymbolByVA( ( ULONGLONG )info.addr, SymTagFunction, &func );
					if ( SUCCEEDED( hr ) && func ) {
						func->get_symIndexId( ( DWORD* )&info.functionUID );
						func->get_length( &info.size );
						VERIFY( 0, func->Release() );
					}

					go = cb( param, &info );

					count++;

					VERIFY( 0, line->Release() );
				}

				VERIFY( 0, enumLineNum->Release() );
				VERIFY( 0, compiland->Release() );
			}

			VERIFY( 0, enumCompilands->Release() );
			VERIFY( 0, file[ i ]->Release() );
		}

		for ( ; i < ( size_t )fetchedFiles; ++i ) {
			file[ i ]->Release();
		}
	}

	free( file );

	VERIFY( 0, enumFiles->Release() );
	VERIFY( 0, symRoot->Release() );

	return ( size_t )count;
}
