/*
================================================================================================
CONFIDENTIAL AND PROPRIETARY INFORMATION/NOT FOR DISCLOSURE WITHOUT WRITTEN PERMISSION 
Copyright 2014 id Software LLC, a ZeniMax Media company. All Rights Reserved. 
================================================================================================
*/
#include "../precompiled.h"

#include <string.h>
#include <assert.h>


#define SCE_CALLSTACK_VERBOSE 0
#define SCE_CALLSTACK_PROFILE 0

/* Maximum section headers to search for elf file parsing. This is used to find the symtab and strtab sections */
#define MAX_SECTION_HEADERS 64
/* You may need to bump the maximum symbol name up if you are noticing that your symbol names are clipped */
#define MAX_SYMBOL_NAME 128
/* This is the maximum amount of runtime modules, that includes system prxs. You probably don't need 256, but this is a safe number */
#define MAX_MODULES 256
/* Symbol window size, For a complete description look at the documention or in the source code for how it's used */
#define SYMBOL_WINDOW_SIZE 2731 /* ends up with a buffer a little bigger than 64K */
/* The maximum amount of calls reported, since we go from top down, it'll include most of the context for the call, but if you need more you can bump this number up */
#define MAX_CALLSTACK_DEPTH 32

#define CALLSTACK_WORK_BUFFER_SIZE ( SYMBOL_WINDOW_SIZE * sizeof( SYMBOL_ENTRY ) )

#pragma pack(push)
#pragma pack(1)
typedef struct _ELF_HEADER {
	uint32_t	m_magic;
	uint8_t		m_bitness;
	uint8_t		m_endianness;
	uint8_t		m_version;
	uint8_t		m_os;
	uint8_t		m_abi_version;
	uint8_t		m_pad[7];
	uint16_t	m_type;
	uint16_t	m_machine;
	uint32_t	m_big_version;
	uint64_t	m_entrypoint;
	uint64_t	m_program_header_offset;
	uint64_t	m_section_header_offset;
	uint32_t	m_flags;
	uint16_t	m_header_size;
	uint16_t	m_program_header_size;
	uint16_t	m_program_header_entries;
	uint16_t	m_section_header_size;
	uint16_t	m_section_header_entries;
	uint16_t	m_section_header_names_index;
} ELF_HEADER;

typedef struct _SECTION_HEADER {
	uint32_t	m_name;
	uint32_t	m_type;
	uint64_t	m_flags;
	uint64_t	m_address;
	uint64_t	m_offset;
	uint64_t	m_size;
	uint32_t	m_link;
	uint32_t	m_info;
	uint64_t	m_address_alignment;
	uint64_t	m_entry_size;
} SECTION_HEADER;

typedef struct _SYMBOL_ENTRY {
	uint32_t	m_name;
	uint8_t		m_info;
	uint8_t		m_other;
	uint16_t	m_section_index;
	uint64_t	m_address;
	uint64_t	m_size;
} SYMBOL_ENTRY;

typedef struct _SceCallstackEntry {
	uintptr_t	m_Address;
	char		m_ModuleName[ PATH_MAX ];
	char		m_Name[ MAX_SYMBOL_NAME ];
	int			padding;
} SceCallstackEntry;

typedef struct _SceCallstack {
	uint32_t			m_EntryCount;
	uint32_t			padding;
	SceCallstackEntry*	m_Entries;
} SceCallstack;

/* callstack assumes that you've removed the base address already */
int loadElf( const char * const path, const uintptr_t addr, char* callstack_name ) {
	SECTION_HEADER section_headers[ MAX_SECTION_HEADERS ];
	ELF_HEADER elf_header;
	FILE* elf_handle;
	int symtab_section = -1;
	int symtab_string_section;
	uint64_t symbols_remaining;
	uint16_t i;
	int num = 0;

	char work_buffer[ CALLSTACK_WORK_BUFFER_SIZE ];

	printf( "Trying to open: %s\n", path );

	/* load up the module */
	elf_handle = fopen( path, "rb" );
	if ( elf_handle == NULL ) {
		printf( "Unable to open: %s\n", path );
		return num;
	}

	printf( "Loaded module: %s\n", path );

	fread( &elf_header, sizeof( ELF_HEADER ), 1, elf_handle);
	_fseeki64( elf_handle, elf_header.m_section_header_offset, SEEK_SET );
	fread( section_headers, sizeof( SECTION_HEADER ) * elf_header.m_section_header_entries, 1, elf_handle );

	symtab_section = -1;

	/* find the symbol table */
	for ( i = 0; i < elf_header.m_section_header_entries; i++ ) {
		if (section_headers[ i ].m_type == 2) { /* only one symbol table section */
			symtab_section = i;
			break;
		}
	}

	if ( symtab_section == -1 ) {
		printf( "Unable to find symbols section!\n" );
		fclose( elf_handle );
		return 0;
	}
	symtab_string_section = section_headers[ symtab_section ].m_link;

	/* loop through the symbols to see if any match our callstack */
	_fseeki64( elf_handle, section_headers[symtab_section].m_offset, SEEK_SET );
	symbols_remaining = section_headers[ symtab_section ].m_size / section_headers[ symtab_section ].m_entry_size;
	printf( "Found (%llu) symbols\n", symbols_remaining );

	while ( symbols_remaining > 0 ) {
		SYMBOL_ENTRY* symbol = ( SYMBOL_ENTRY* )work_buffer;
		uint64_t symbol_read_count = min( symbols_remaining, SYMBOL_WINDOW_SIZE );
		uint64_t symbol_index;
		fread( symbol, sizeof( SYMBOL_ENTRY ), ( long )symbol_read_count, elf_handle );

		for ( symbol_index = 0; symbol_index < symbol_read_count; symbol_index++ ) {
			if ( symbol[ symbol_index ].m_address < addr && symbol[ symbol_index ].m_address + symbol[ symbol_index ].m_size > addr ) {
				char symbol_name[ MAX_SYMBOL_NAME ];
				_fseeki64( elf_handle, ( section_headers[ symtab_string_section ].m_offset + symbol[ symbol_index ].m_name ), SEEK_SET );
				fread( symbol_name, MAX_SYMBOL_NAME, 1, elf_handle );
				symbol_name[ sizeof( symbol_name ) - 1 ] = 0;
				strncpy( callstack_name, symbol_name, MAX_SYMBOL_NAME ); /* todo: uhhh... this is terrible, lol */
				num++;
				symbol_index = symbol_read_count;
				break;
			}
		}
		symbols_remaining -= symbol_read_count;
	}

	fclose( elf_handle );
	return num;
}

#if 000

/* static data */
const uint32_t				CALLSTACK_WORK_BUFFER_SIZE = SYMBOL_WINDOW_SIZE * sizeof(SYMBOL_ENTRY);
static SceCallstackEntry	g_callstack_entries[MAX_CALLSTACK_DEPTH];
static SceCallstack			g_callstack;
static char					g_callstack_file_fullpath[PATH_MAX];
static char					g_callstack_work_buffer[CALLSTACK_WORK_BUFFER_SIZE];
#if SCE_ORBIS_SDK_VERSION >= 0x02500000U
		static SceDbgCallFrame		g_callframe[MAX_CALLSTACK_DEPTH];
#endif

#if SCE_ORBIS_SDK_VERSION < 0x02500000U

inline uintptr_t ReturnAddress(uint8_t _level)
{
	void* ret = 0;
	switch (_level)
	{
		case 0: ret = __builtin_return_address(0); break;
		case 1: ret = __builtin_return_address(1); break;
		case 2: ret = __builtin_return_address(2); break;
		case 3: ret = __builtin_return_address(3); break;
		case 4: ret = __builtin_return_address(4); break;
		case 5: ret = __builtin_return_address(5); break;
		case 6: ret = __builtin_return_address(6); break;
		case 7: ret = __builtin_return_address(7); break;
		case 8: ret = __builtin_return_address(8); break;
		case 9: ret = __builtin_return_address(9); break;
		case 10: ret = __builtin_return_address(10); break;
		case 11: ret = __builtin_return_address(11); break;
		case 12: ret = __builtin_return_address(12); break;
		case 13: ret = __builtin_return_address(13); break;
		case 14: ret = __builtin_return_address(14); break;
		case 15: ret = __builtin_return_address(15); break;
		case 16: ret = __builtin_return_address(16); break;
		case 17: ret = __builtin_return_address(17); break;
		case 18: ret = __builtin_return_address(18); break;
		case 19: ret = __builtin_return_address(19); break;
		case 20: ret = __builtin_return_address(20); break;
		case 21: ret = __builtin_return_address(21); break;
		case 22: ret = __builtin_return_address(22); break;
		case 23: ret = __builtin_return_address(23); break;
		case 24: ret = __builtin_return_address(24); break;
		case 25: ret = __builtin_return_address(25); break;
		case 26: ret = __builtin_return_address(26); break;
		case 27: ret = __builtin_return_address(27); break;
		case 28: ret = __builtin_return_address(28); break;
		case 29: ret = __builtin_return_address(29); break;
		case 30: ret = __builtin_return_address(30); break;
		case 31: ret = __builtin_return_address(31); break;
		case 32: ret = __builtin_return_address(32); break;
		case 33: ret = __builtin_return_address(33); break;
		case 34: ret = __builtin_return_address(34); break;
		case 35: ret = __builtin_return_address(35); break;
		case 36: ret = __builtin_return_address(36); break;
		case 37: ret = __builtin_return_address(37); break;
		case 38: ret = __builtin_return_address(38); break;
		case 39: ret = __builtin_return_address(39); break;
		case 40: ret = __builtin_return_address(40); break;
		case 41: ret = __builtin_return_address(41); break;
		case 42: ret = __builtin_return_address(42); break;
		case 43: ret = __builtin_return_address(43); break;
		case 44: ret = __builtin_return_address(44); break;
		case 45: ret = __builtin_return_address(45); break;
		case 46: ret = __builtin_return_address(46); break;
		case 47: ret = __builtin_return_address(47); break;
		case 48: ret = __builtin_return_address(48); break;
		case 49: ret = __builtin_return_address(49); break;
		case 50: ret = __builtin_return_address(50); break;
		case 51: ret = __builtin_return_address(51); break;
		case 52: ret = __builtin_return_address(52); break;
		case 53: ret = __builtin_return_address(53); break;
		case 54: ret = __builtin_return_address(54); break;
		case 55: ret = __builtin_return_address(55); break;
		case 56: ret = __builtin_return_address(56); break;
		case 57: ret = __builtin_return_address(57); break;
		case 58: ret = __builtin_return_address(58); break;
		case 59: ret = __builtin_return_address(59); break;
		case 60: ret = __builtin_return_address(60); break;
		case 61: ret = __builtin_return_address(61); break;
		case 62: ret = __builtin_return_address(62); break;
		case 63: ret = __builtin_return_address(63); break;
		case 64: ret = __builtin_return_address(64); break;
		case 65: ret = __builtin_return_address(65); break;
		case 66: ret = __builtin_return_address(66); break;
		case 67: ret = __builtin_return_address(67); break;
		case 68: ret = __builtin_return_address(68); break;
		case 69: ret = __builtin_return_address(69); break;
		case 70: ret = __builtin_return_address(70); break;
		case 71: ret = __builtin_return_address(71); break;
		case 72: ret = __builtin_return_address(72); break;
		case 73: ret = __builtin_return_address(73); break;
		case 74: ret = __builtin_return_address(74); break;
		case 75: ret = __builtin_return_address(75); break;
		case 76: ret = __builtin_return_address(76); break;
		case 77: ret = __builtin_return_address(77); break;
		case 78: ret = __builtin_return_address(78); break;
		case 79: ret = __builtin_return_address(79); break;
		case 80: ret = __builtin_return_address(80); break;
		case 81: ret = __builtin_return_address(81); break;
		case 82: ret = __builtin_return_address(82); break;
		case 83: ret = __builtin_return_address(83); break;
		case 84: ret = __builtin_return_address(84); break;
		case 85: ret = __builtin_return_address(85); break;
		case 86: ret = __builtin_return_address(86); break;
		case 87: ret = __builtin_return_address(87); break;
		case 88: ret = __builtin_return_address(88); break;
		case 89: ret = __builtin_return_address(89); break;
		case 90: ret = __builtin_return_address(90); break;
		case 91: ret = __builtin_return_address(91); break;
		case 92: ret = __builtin_return_address(92); break;
		case 93: ret = __builtin_return_address(93); break;
		case 94: ret = __builtin_return_address(94); break;
		case 95: ret = __builtin_return_address(95); break;
		case 96: ret = __builtin_return_address(96); break;
		case 97: ret = __builtin_return_address(97); break;
		case 98: ret = __builtin_return_address(98); break;
		case 99: ret = __builtin_return_address(99); break;
		case 100: ret = __builtin_return_address(100); break;
		case 101: ret = __builtin_return_address(101); break;
		case 102: ret = __builtin_return_address(102); break;
		case 103: ret = __builtin_return_address(103); break;
		case 104: ret = __builtin_return_address(104); break;
		case 105: ret = __builtin_return_address(105); break;
		case 106: ret = __builtin_return_address(106); break;
		case 107: ret = __builtin_return_address(107); break;
		case 108: ret = __builtin_return_address(108); break;
		case 109: ret = __builtin_return_address(109); break;
		case 110: ret = __builtin_return_address(110); break;
		case 111: ret = __builtin_return_address(111); break;
		case 112: ret = __builtin_return_address(112); break;
		case 113: ret = __builtin_return_address(113); break;
		case 114: ret = __builtin_return_address(114); break;
		case 115: ret = __builtin_return_address(115); break;
		case 116: ret = __builtin_return_address(116); break;
		case 117: ret = __builtin_return_address(117); break;
		case 118: ret = __builtin_return_address(118); break;
		case 119: ret = __builtin_return_address(119); break;
		case 120: ret = __builtin_return_address(120); break;
		case 121: ret = __builtin_return_address(121); break;
		case 122: ret = __builtin_return_address(122); break;
		case 123: ret = __builtin_return_address(123); break;
		case 124: ret = __builtin_return_address(124); break;
		case 125: ret = __builtin_return_address(125); break;
		case 126: ret = __builtin_return_address(126); break;
		case 127: ret = __builtin_return_address(127); break;
	}
	return (uintptr_t)ret;
}

#endif

/* /////////////////////////////////////////////////////////////////////////////////////////////////// */
uintptr_t get_module_start_address(SceKernelModuleInfo* module_info)
{
	/* we only use one segment because there is the only one with code so far. If this changes we can loop through all of the modules base addresses. */
	uintptr_t module_start_address = 0;
	for (uint32_t i = 0; i < module_info->numSegments && module_start_address == 0; i++)
	{
		if (module_info->segmentInfo[i].prot & SCE_KERNEL_PROT_CPU_EXEC) /* we only care about executable sections */
		{
			module_start_address = (uintptr_t)module_info->segmentInfo[i].baseAddr;
		}
	}
	return module_start_address;
}

/* /////////////////////////////////////////////////////////////////////////////////////////////////// */
bool is_module_in_callstack(SceKernelModuleInfo* module_info, SceCallstack* callstack)
{
	/* find out if anyone in the callstack belong to this module */
	bool module_in_callstack = false;
	for (uint32_t callstack_index = 0; callstack_index < callstack->m_EntryCount; callstack_index++)
	{
		if (callstack->m_Entries[callstack_index].m_Address >(uint64_t)module_info->segmentInfo[0].baseAddr &&
			callstack->m_Entries[callstack_index].m_Address < (uint64_t)module_info->segmentInfo[0].baseAddr + module_info->segmentInfo[0].size)
		{
			strcpy(callstack->m_Entries[callstack_index].m_ModuleName, module_info->name);
			module_in_callstack = true;
		}
	}
	return module_in_callstack;
}

/* /////////////////////////////////////////////////////////////////////////////////////////////////// *\
\* ELF file symbols parsing                                                                            */
void get_elf_symbols_for_callstack_from_module(SceKernelModuleInfo* module_info, SceCallstack* callstack)
{
	if (!is_module_in_callstack(module_info, callstack))
	{
		printf( "No callstack for module: %s, early out\n", module_info->name );
		/* early out if we don't have any addresses from this module in our callstack */
		return;
	}

	uintptr_t module_start_address = get_module_start_address(module_info);
	if (module_start_address == 0)
	{
		printf( "Unable to find executable section in module %s!\n", module_info->name );
		return;
	}

	/* try and load the module from the same directory as the elf file. */
	memset(g_callstack_file_fullpath, 0, PATH_MAX);
	if (sceDbgGetExecutablePath(g_callstack_file_fullpath, PATH_MAX) == SCE_SYSTEM_SERVICE_ERROR_REJECTED)
	{
		strcat(g_callstack_file_fullpath, "/app0/");
		strcat(g_callstack_file_fullpath, module_info->name);
		printf( "sceDbgGetExecutablePath returned SCE_SYSTEM_SERVICE_ERROR_REJECTED: trying module: %s\n", g_callstack_file_fullpath );
	}
	else
	{
		char* head = strrchr(g_callstack_file_fullpath, '\\');
		if (head)
		{
			head++;
			*head = 0;
			strcat(head, module_info->name);
		}
	}

}

/* //////////////////////////////////////////////////////////////////////////////////////////////////// *\
\* little helper for kerenel_fgets                                                                      */
int find_linefeed(char* string, int length)
{
	for (int i = 0; i < length; i++)
	{
		if (string[i] == '\r')
		{
			return i;
		}
	}
	return -1;
}

/* /////////////////////////////////////////////////////////////////////////////////////////////////// *\
\* fgets for files opened with sceKernelOpen()                                                         */
char* kernel_fgets(int file_handle)
{
	static uint32_t buffer_start = 0;
	static uint32_t buffer_end = 0;

	int line_feed_index = find_linefeed(&g_callstack_work_buffer[buffer_start], buffer_end - buffer_start);
	if (line_feed_index == -1)
	{
		/* move the buffer data down */
		buffer_end -= buffer_start;
		for (uint32_t i = 0; i < buffer_end; i++)
		{
			g_callstack_work_buffer[i] = g_callstack_work_buffer[buffer_start + i];
		}
		memset(&g_callstack_work_buffer[buffer_end], 0, buffer_start);
		buffer_start = 0;

		int amount_read = sceKernelRead(file_handle, &g_callstack_work_buffer[buffer_end], CALLSTACK_WORK_BUFFER_SIZE - buffer_end);
		buffer_end += amount_read;
		if (amount_read <= 0)
		{
			return NULL;
		}
		line_feed_index = find_linefeed(g_callstack_work_buffer, CALLSTACK_WORK_BUFFER_SIZE);
		if (line_feed_index == -1)
		{
			printf( "!!kernel_fgets didn't find a line_feed in: %s\n", g_callstack_work_buffer);
			return NULL;
		}
	}

	g_callstack_work_buffer[buffer_start + line_feed_index] = 0;
	int head = buffer_start;
	buffer_start += line_feed_index + 1;

	return &g_callstack_work_buffer[head];
}

/* /////////////////////////////////////////////////////////////////////////////////////////////////// *\
\* MAP file symbols parsing                                                                            */
void get_map_symbols_for_callstack_from_module(SceKernelModuleInfo* module_info, SceCallstack* callstack)
{
	if (!is_module_in_callstack(module_info, callstack))
	{
		printf( "No callstack for module: %s, early out\n", module_info->name);
		/* early out if we don't have any addresses from this module in our callstack */
		return;
	}

	uintptr_t module_start_address = get_module_start_address(module_info);
	if (module_start_address == 0)
	{
		printf( "Unable to find executable section in module %s!\n", module_info->name);
		return;
	}

	/* try and load the map from the same directory as the elf file. */
	memset(g_callstack_file_fullpath, 0, PATH_MAX);
	strcat(g_callstack_file_fullpath, "/app0/");
	strcat(g_callstack_file_fullpath, module_info->name);
	char* head = strrchr(g_callstack_file_fullpath, '.');
	if (head)
	{
		head++;
		*head = 0;
		strcat(g_callstack_file_fullpath, "map");
	}

	printf( "Trying to open: %s\n", g_callstack_file_fullpath);
	/* load up the module */
	int map_handle = sceKernelOpen(g_callstack_file_fullpath, SCE_KERNEL_O_RDONLY, SCE_KERNEL_S_INONE);
	if (map_handle < 0)
	{
		printf( "Unable to open: %s (%08X)\n", g_callstack_file_fullpath, map_handle);
		return;
	}

	printf( "Loaded module: %s\n", g_callstack_file_fullpath);

	char* current_line = kernel_fgets(map_handle);
	while (current_line != NULL)
	{
		/* check to the first field being an address. if not, we can skip it */
		uint64_t address = strtoull(current_line, NULL, 16);
		if (address != 0)
		{
			/* skip passed the address and grab the size */
			uint64_t size = strtoull(&current_line[9], NULL, 16);
			if (size != 0)
			{
				/* check to make sure that we're a function not a section */
				if (current_line[23] == '0' && current_line[22] == ' ')
				{
					/* see if we match before we mess about with more string stuff */
					for (uint32_t callstack_index = 0; callstack_index < callstack->m_EntryCount; callstack_index++)
					{
						uint64_t translated_address = callstack->m_Entries[callstack_index].m_Address - module_start_address;
						if (address < translated_address && address + size > translated_address)
						{
							int line_index = 24; /* jump the address, size and other stuff */

							/* eat whitespace */
							while (current_line[line_index] == ' ')
							{
								line_index++;
							}
							strcpy(callstack->m_Entries[callstack_index].m_Name, &current_line[line_index]);
						}
					}
				}
			}
		}
		current_line = kernel_fgets(map_handle);
	}

	sceKernelClose(map_handle);

}

/* /////////////////////////////////////////////////////////////////////////////////////////////////// *\
/* always inline so we don't have an extra function in the callstack in debug vs release when it may get inlined via the optimization flags */
__attribute__((__always_inline__))  uint32_t get_callstack_addresses(SceCallstackEntry* stack_entries, uint32_t max_depth)
{
	uint32_t depth = 0;
	sceDbgBacktraceSelf(g_callframe, sizeof(g_callframe), &depth, SCE_DBG_BACKTRACE_MODE_DONT_EXCEED);
	if ( depth > 0 )	/* in case there is an error in backtrace self, we don't want to wrap around */
		depth--;	/* we're going to remove the top of the stack since that's the get callstack function itself */

	for (uint32_t i = 0; i < depth; i++)
	{
		stack_entries[i].m_Address = g_callframe[i + 1].pc;	/* adjust each one down since we're not reporting the get callstack function */
	}
	return depth;
}

/* /////////////////////////////////////////////////////////////////////////////////////////////////// */
void get_symbols_for_callstack(SceCallstack* callstack, SceDbgUserChannel user_channel, SCE_CALLSTACK_SYMBOL_LOCATION location)
{
	if (location == SCE_CALLSTACK_SYMBOLS_LOCATION_None)
	{
		printf( "Called get_symbols_for_callstack() with SCE_CALLSTACK_SYMBOLS_LOCATION_None!\n" );
		return;
	}

	/* check any loaded modules */
	SceKernelModule module_list[MAX_MODULES];
	size_t module_count;
	int ret = sceKernelGetModuleList(module_list, MAX_MODULES, &module_count);
	if (ret < 0)
	{
		sceDbgUserChannelPrintf(user_channel, "sceKernelGetModuleList failed with: %08X\n", ret);
		return;
	}

	for (size_t i = 0; i < module_count; i++)
	{
		SceKernelModuleInfo module_info;
		module_info.size = sizeof(SceKernelModuleInfo);
		ret = sceKernelGetModuleInfo(module_list[i], &module_info);
		if (ret < 0)
		{
			sceDbgUserChannelPrintf(user_channel, "sceKernelGetModuleInfo failed with: %08X\n", ret);
			return;
		}

		switch (location)
		{
			case SCE_CALLSTACK_SYMBOLS_LOCATION_Elf:
			{
				get_elf_symbols_for_callstack_from_module(&module_info, callstack);
				break;
			}
			case SCE_CALLSTACK_SYMBOLS_LOCATION_Map:
			{
				get_map_symbols_for_callstack_from_module(&module_info, callstack);
				break;
			}
			default:
			{
				printf( "Unknown symbols location: %d!\n", location );
				break;
			}
		}
	}
}

/* ///////////////////////////////////////////////////////////////////////////////////////////////////  */
void DumpCallstackToTTY(SceDbgUserChannel user_channel, SCE_CALLSTACK_SYMBOL_LOCATION location, uint32_t numToSkip, uint32_t maxLevels)
{
	/* simple spin acquire */
	/* do the callstack */
	g_callstack.m_Entries = g_callstack_entries;
	g_callstack.m_EntryCount = MAX_CALLSTACK_DEPTH;

	g_callstack.m_EntryCount = get_callstack_addresses(g_callstack.m_Entries, g_callstack.m_EntryCount);

	if (location != SCE_CALLSTACK_SYMBOLS_LOCATION_None)
	{
		get_symbols_for_callstack(&g_callstack, user_channel, location);
	}

	sceDbgUserChannelPrintf(user_channel, "Callstack:\n");
				numToSkip	= (numToSkip<g_callstack.m_EntryCount) ? numToSkip : 0;
	uint32_t	numToDump	= g_callstack.m_EntryCount-numToSkip;
				numToDump	= (numToDump > maxLevels) ? maxLevels : numToDump;
	for (uint32_t i = numToSkip; i < (numToSkip+numToDump); i++)
	{
		sceDbgUserChannelPrintf(user_channel, "%s:(%016lX) %s\n", g_callstack.m_Entries[i].m_ModuleName, g_callstack.m_Entries[i].m_Address, g_callstack.m_Entries[i].m_Name);
	}
}



/* ///////////////////////////////////////////////////////////////////////////////////////////////////  */
int DumpCallstackToBuffer(char* pBuffer, uint32_t bufSize, SCE_CALLSTACK_SYMBOL_LOCATION location, uint32_t numToSkip, uint32_t maxLevels)
{
	/* simple spin acquire */
	/* do the callstack */
	g_callstack.m_Entries = g_callstack_entries;
	g_callstack.m_EntryCount = MAX_CALLSTACK_DEPTH;

	g_callstack.m_EntryCount = get_callstack_addresses(g_callstack.m_Entries, g_callstack.m_EntryCount);

	if (location != SCE_CALLSTACK_SYMBOLS_LOCATION_None)
	{
		get_symbols_for_callstack(&g_callstack, SCE_DBG_USER_CHANNEL_2, location);
	}

	assert(bufSize>0);
				numToSkip	= (numToSkip<g_callstack.m_EntryCount) ? numToSkip : 0;
	uint32_t	numToDump	= g_callstack.m_EntryCount-numToSkip;
				numToDump	= (numToDump > maxLevels) ? maxLevels : numToDump;
	int			idx			= 0;
	for (uint32_t i = numToSkip; i < (numToSkip+numToDump); i++)
	{
		idx += snprintf(&pBuffer[idx], bufSize-idx, "%s:(%016lX) %s\n", g_callstack.m_Entries[i].m_ModuleName, g_callstack.m_Entries[i].m_Address, g_callstack.m_Entries[i].m_Name);
	}

	return idx;
}


/* ///////////////////////////////////////////////////////////////////////////////////////////////////  */
int GetCallstackStaticDataSize()
{
	int size = sizeof(g_callstack_entries) + sizeof(g_callstack) + sizeof(g_callstack_file_fullpath) + sizeof(g_callstack_work_buffer);
	size += sizeof(g_callframe);
	return size;
}

#endif /* 000 */

#pragma pack(pop)
