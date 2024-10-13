#include "symbolfinder.hpp"
#include "platform.hpp"

#include <CoreServices/CoreServices.h>
#include <mach/task.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <string.h>
#include <sys/mman.h>
#include <dlfcn.h>

#if defined ARCHITECTURE_X86

#define LC_SEGMENT_VALUE LC_SEGMENT
#define MH_MAGIC_VALUE MH_MAGIC
#define CPU_TYPE CPU_TYPE_I386
#define CPU_SUBTYPE CPU_SUBTYPE_I386_ALL

typedef struct mach_header mach_header_t;
typedef struct segment_command segment_command_t;
typedef struct nlist nlist_t;
	
#elif defined ARCHITECTURE_X86_64

#define LC_SEGMENT_VALUE LC_SEGMENT_64
#define MH_MAGIC_VALUE MH_MAGIC_64
#define CPU_TYPE CPU_TYPE_X86_64
#define CPU_SUBTYPE CPU_SUBTYPE_X86_64_ALL

typedef struct mach_header_64 mach_header_t;
typedef struct segment_command_64 segment_command_t;
typedef struct nlist_64 nlist_t;
	
#endif

typedef struct load_command load_command_t;
typedef struct symtab_command symtab_command_t;

struct DynLibInfo
{
	void *baseAddress;
	size_t memorySize;
};

static struct dyld_all_image_infos *GetImageList( )
{
	task_dyld_info_data_t dyld_info;
	mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
	task_info( mach_task_self( ), TASK_DYLD_INFO, reinterpret_cast<task_info_t>( &dyld_info ), &count );
	return reinterpret_cast<struct dyld_all_image_infos *>( dyld_info.all_image_info_addr );
}

static const struct dyld_all_image_infos *imageList = GetImageList( );

void *SymbolFinder::FindPattern( const void *handle, const uint8_t *pattern, size_t len, const void *start )
{
	DynLibInfo lib;
	memset( &lib, 0, sizeof( DynLibInfo ) );
	if( !GetLibraryInfo( handle, lib ) )
		return nullptr;

	uint8_t *ptr = reinterpret_cast<uint8_t *>( start > lib.baseAddress ? const_cast<void *>( start ) : lib.baseAddress );
	uint8_t *end = reinterpret_cast<uint8_t *>( lib.baseAddress ) + lib.memorySize - len;
	bool found = true;
	while( ptr < end )
	{
		for( size_t i = 0; i < len; ++i )
			if( pattern[i] != '\x2A' && pattern[i] != ptr[i] )
			{
				found = false;
				break;
			}

		if( found )
			return ptr;

		++ptr;
		found = true;
	}

	return nullptr;
}

void *SymbolFinder::FindPatternFromBinary( const char *name, const uint8_t *pattern, size_t len, const void *start )
{
	void *binary = dlopen( name, RTLD_LAZY | RTLD_NOLOAD );
	if( binary != nullptr)
	{
		void *symbol_pointer = FindPattern( binary, pattern, len, start );
		dlclose( binary );
		return symbol_pointer;
	}

	return nullptr;
}

void *SymbolFinder::FindSymbol( const void *handle, const char *symbol )
{
	DynLibInfo lib;
	if( !GetLibraryInfo( handle, lib ) )
		return nullptr;

	uintptr_t dlbase = reinterpret_cast<uintptr_t>( lib.baseAddress );
	SymbolTable *libtable = nullptr;
	const auto it = symbolTables.find( dlbase );
	if( it != symbolTables.end( ) )
		libtable = &it->second;

	if( libtable == nullptr )
		libtable = &symbolTables[dlbase];

	auto &table = libtable->table;
	void *symbol_ptr = table[symbol];
	if( symbol_ptr != nullptr )
		return symbol_ptr;

	segment_command_t *linkedit_hdr = nullptr;
	symtab_command_t *symtab_hdr = nullptr;
	mach_header_t *file_hdr = reinterpret_cast<mach_header_t *>( dlbase );
	load_command_t *loadcmds = reinterpret_cast<load_command_t *>( dlbase + sizeof( mach_header_t ) );
	uint32_t loadcmd_count = file_hdr->ncmds;
	for( uint32_t i = 0; i < loadcmd_count; i++ )
	{
		if( loadcmds->cmd == LC_SEGMENT_VALUE && linkedit_hdr == nullptr )
		{
			segment_command_t *seg = reinterpret_cast<segment_command_t *>( loadcmds );
			if( strcmp( seg->segname, "__LINKEDIT" ) == 0 )
			{
				linkedit_hdr = seg;
				if( symtab_hdr != nullptr)
					break;
			}
		}
		else if( loadcmds->cmd == LC_SYMTAB )
		{
			symtab_hdr = reinterpret_cast<symtab_command_t *>( loadcmds );
			if( linkedit_hdr != nullptr )
				break;
		}

		loadcmds = reinterpret_cast<load_command_t *>( reinterpret_cast<uintptr_t>( loadcmds ) + loadcmds->cmdsize );
	}

	if( linkedit_hdr == nullptr || symtab_hdr == nullptr || symtab_hdr->symoff == 0 || symtab_hdr->stroff == 0 )
		return nullptr;

	uintptr_t linkedit_addr = dlbase + linkedit_hdr->vmaddr;
	nlist_t *symtab = reinterpret_cast<nlist_t *>( linkedit_addr + symtab_hdr->symoff - linkedit_hdr->fileoff );
	const char *strtab = reinterpret_cast<const char *>( linkedit_addr + symtab_hdr->stroff - linkedit_hdr->fileoff );
	uint32_t symbol_count = symtab_hdr->nsyms;
	void *symbol_pointer = nullptr;
	for( uint32_t i = libtable->last_pos; i < symbol_count; i++ )
	{
		nlist_t &sym = symtab[i];
		const char *sym_name = strtab + sym.n_un.n_strx + 1;
		if( sym.n_sect == NO_SECT )
			continue;

		void *symptr = reinterpret_cast<void *>( dlbase + sym.n_value );
		table[sym_name] = symptr;
		if( strcmp( sym_name, symbol ) == 0 )
		{
			libtable->last_pos = ++i;
			symbol_pointer = symptr;
			break;
		}
	}

	return symbol_pointer;
}

void *SymbolFinder::FindSymbolFromBinary( const char *name, const char *symbol )
{
	void *binary = dlopen( name, RTLD_LAZY | RTLD_NOLOAD );
	if( binary != nullptr )
	{
		void *symbol_pointer = FindSymbol( binary, symbol );
		dlclose( binary );
		return symbol_pointer;
	}

	return nullptr;
}

void *SymbolFinder::Resolve( const void *handle, const char *data, size_t len, const void *start )
{
	if( len == 0 && data[0] == '@' )
		return FindSymbol( handle, ++data );

	if( len != 0 )
		return FindPattern( handle, reinterpret_cast<const uint8_t *>( data ), len, start );

	return nullptr;
}

void *SymbolFinder::ResolveOnBinary( const char *name, const char *data, size_t len, const void *start )
{
	if( len == 0 && data[0] == '@' )
		return FindSymbolFromBinary( name, ++data );

	if( len != 0 )
		return FindPatternFromBinary( name, reinterpret_cast<const uint8_t *>( data ), len, start );

	return nullptr;
}

bool SymbolFinder::GetLibraryInfo( const void *handle, DynLibInfo &lib )
{
	if( handle == nullptr )
		return false;

	uintptr_t baseAddr = 0;
	for( uint32_t i = 1; i < imageList->infoArrayCount; ++i )
	{
		const struct dyld_image_info &info = imageList->infoArray[i];
		void *h = dlopen( info.imageFilePath, RTLD_LAZY | RTLD_NOLOAD );
		if( h == handle )
		{
			baseAddr = reinterpret_cast<uintptr_t>( info.imageLoadAddress );
			dlclose( h );
			break;
		}

		dlclose( h );
	}

	if( baseAddr == 0 )
		return false;

	mach_header_t *file = reinterpret_cast<mach_header_t *>( baseAddr );
	if( file->magic != MH_MAGIC_VALUE )
		return false;

	if( file->cputype != CPU_TYPE || file->cpusubtype != CPU_SUBTYPE )
		return false;

	if( file->filetype != MH_DYLIB )
		return false;

	uint32_t cmd_count = file->ncmds;
	segment_command_t *seg = reinterpret_cast<segment_command_t *>( baseAddr + sizeof( mach_header_t ) );

	for( uint32_t i = 0; i < cmd_count; ++i )
	{
		if( seg->cmd == LC_SEGMENT_VALUE )
			lib.memorySize += seg->vmsize;

		seg = reinterpret_cast<segment_command_t *>( reinterpret_cast<uintptr_t>( seg ) + seg->cmdsize );
	}

	lib.baseAddress = reinterpret_cast<void *>( baseAddr );
	return true;
}
