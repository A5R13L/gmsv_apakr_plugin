#include "symbolfinder.hpp"
#include "platform.hpp"

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

struct DynLibInfo
{
	void *baseAddress;
	size_t memorySize;
};

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
	HMODULE binary = nullptr;
	if( GetModuleHandleEx( 0, name, &binary ) == TRUE && binary != nullptr )
	{
		void *symbol_pointer = FindPattern( binary, pattern, len, start );
		FreeModule( binary );
		return symbol_pointer;
	}

	return nullptr;
}

void *SymbolFinder::FindSymbol( const void *handle, const char *symbol )
{
	return GetProcAddress( reinterpret_cast<HMODULE>( const_cast<void *>( handle ) ), symbol );
}

void *SymbolFinder::FindSymbolFromBinary( const char *name, const char *symbol )
{
	HMODULE binary = nullptr;
	if( GetModuleHandleEx( 0, name, &binary ) == TRUE && binary != nullptr )
	{
		void *symbol_pointer = FindSymbol( binary, symbol );
		FreeModule( binary );
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

#if defined ARCHITECTURE_X86
	
	const WORD IMAGE_FILE_MACHINE = IMAGE_FILE_MACHINE_I386;
	
#elif defined ARCHITECTURE_X86_64
	
	const WORD IMAGE_FILE_MACHINE = IMAGE_FILE_MACHINE_AMD64;
	
#endif

	MEMORY_BASIC_INFORMATION info;
	if( VirtualQuery( handle, &info, sizeof( info ) ) == FALSE )
		return false;

	uintptr_t baseAddr = reinterpret_cast<uintptr_t>( info.AllocationBase );

	IMAGE_DOS_HEADER *dos = reinterpret_cast<IMAGE_DOS_HEADER *>( baseAddr );
	IMAGE_NT_HEADERS *pe = reinterpret_cast<IMAGE_NT_HEADERS *>( baseAddr + dos->e_lfanew );
	IMAGE_FILE_HEADER *file = &pe->FileHeader;
	IMAGE_OPTIONAL_HEADER *opt = &pe->OptionalHeader;

	if( dos->e_magic != IMAGE_DOS_SIGNATURE || pe->Signature != IMAGE_NT_SIGNATURE || opt->Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC )
		return false;

	if( file->Machine != IMAGE_FILE_MACHINE )
		return false;

	if( ( file->Characteristics & IMAGE_FILE_DLL ) == 0 )
		return false;

	lib.memorySize = opt->SizeOfImage;
	lib.baseAddress = reinterpret_cast<void *>( baseAddr );
	return true;
}
