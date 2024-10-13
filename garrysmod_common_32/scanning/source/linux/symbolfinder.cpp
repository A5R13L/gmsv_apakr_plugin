#include "symbolfinder.hpp"
#include "platform.hpp"

#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define PAGE_ALIGN_UP( x ) ( ( x + PAGE_SIZE - 1 ) & ~( PAGE_SIZE - 1 ) )

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

#if defined ARCHITECTURE_X86
	
	typedef Elf32_Ehdr Elf_Ehdr;
	typedef Elf32_Shdr Elf_Shdr;
	typedef Elf32_Sym Elf_Sym;
#define ELF_ST_TYPE ELF32_ST_TYPE
	
#elif defined ARCHITECTURE_X86_64
	
	typedef Elf64_Ehdr Elf_Ehdr;
	typedef Elf64_Shdr Elf_Shdr;
	typedef Elf64_Sym Elf_Sym;
#define ELF_ST_TYPE ELF64_ST_TYPE
	
#endif
	
	const struct link_map *dlmap = reinterpret_cast<const struct link_map *>( handle );
	SymbolTable *libtable = nullptr;
	const auto it = symbolTables.find( dlmap->l_addr );
	if( it != symbolTables.end( ) )
		libtable = &it->second;

	if( libtable == nullptr )
		libtable = &symbolTables[dlmap->l_addr];

	auto &table = libtable->table;
	void *symbol_ptr = table[symbol];
	if( symbol_ptr != nullptr )
		return symbol_ptr;

	struct stat64 dlstat;
	int dlfile = open( dlmap->l_name, O_RDONLY );
	if( dlfile == -1 || fstat64( dlfile, &dlstat ) == -1 )
	{
		close( dlfile );
		return nullptr;
	}

	Elf_Ehdr *file_hdr = reinterpret_cast<Elf_Ehdr *>( mmap( 0, dlstat.st_size, PROT_READ, MAP_PRIVATE, dlfile, 0 ) );
	uintptr_t map_base = reinterpret_cast<uintptr_t>( file_hdr );
	close( dlfile );
	if( file_hdr == MAP_FAILED )
		return nullptr;

	if( file_hdr->e_shoff == 0 || file_hdr->e_shstrndx == SHN_UNDEF )
	{
		munmap( file_hdr, dlstat.st_size );
		return nullptr;
	}

	Elf_Shdr *symtab_hdr = nullptr, *strtab_hdr = nullptr;
	Elf_Shdr *sections = reinterpret_cast<Elf_Shdr *>( map_base + file_hdr->e_shoff );
	uint16_t section_count = file_hdr->e_shnum;
	Elf_Shdr *shstrtab_hdr = &sections[file_hdr->e_shstrndx];
	const char *shstrtab = reinterpret_cast<const char *>( map_base + shstrtab_hdr->sh_offset );
	for( uint16_t i = 0; i < section_count; i++ )
	{
		Elf_Shdr &hdr = sections[i];
		const char *section_name = shstrtab + hdr.sh_name;
		if( strcmp( section_name, ".symtab" ) == 0 )
			symtab_hdr = &hdr;
		else if( strcmp( section_name, ".strtab" ) == 0 )
			strtab_hdr = &hdr;
	}

	if( symtab_hdr == nullptr || strtab_hdr == nullptr )
	{
		munmap( file_hdr, dlstat.st_size );
		return nullptr;
	}

	Elf_Sym *symtab = reinterpret_cast<Elf_Sym *>( map_base + symtab_hdr->sh_offset );
	const char *strtab = reinterpret_cast<const char *>( map_base + strtab_hdr->sh_offset );
	uint32_t symbol_count = symtab_hdr->sh_size / symtab_hdr->sh_entsize;
	void *symbol_pointer = nullptr;
	for( uint32_t i = libtable->last_pos; i < symbol_count; i++ )
	{
		Elf_Sym &sym = symtab[i];
		uint8_t sym_type = ELF_ST_TYPE( sym.st_info );
		const char *sym_name = strtab + sym.st_name;

		if( sym.st_shndx == SHN_UNDEF || ( sym_type != STT_FUNC && sym_type != STT_OBJECT ) )
			continue;

		void *symptr = reinterpret_cast<void *>( dlmap->l_addr + sym.st_value );
		table[sym_name] = symptr;
		if( strcmp( sym_name, symbol ) == 0 )
		{
			libtable->last_pos = ++i;
			symbol_pointer = symptr;
			break;
		}
	}

	munmap( file_hdr, dlstat.st_size );
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

#if defined ARCHITECTURE_X86
	
	typedef Elf32_Ehdr Elf_Ehdr;
	typedef Elf32_Phdr Elf_Phdr;
	const unsigned char ELFCLASS = ELFCLASS32;
	const uint16_t EM = EM_386;
	
#elif defined ARCHITECTURE_X86_64
	
	typedef Elf64_Ehdr Elf_Ehdr;
	typedef Elf64_Phdr Elf_Phdr;
	const unsigned char ELFCLASS = ELFCLASS64;
	const uint16_t EM = EM_X86_64;
	
#endif
	
	const struct link_map *map = static_cast<const struct link_map *>( handle );
	uintptr_t baseAddr = reinterpret_cast<uintptr_t>( map->l_addr );
	Elf_Ehdr *file = reinterpret_cast<Elf_Ehdr *>( baseAddr );
	if( memcmp( ELFMAG, file->e_ident, SELFMAG ) != 0 )
		return false;

	if( file->e_ident[EI_VERSION] != EV_CURRENT )
		return false;

	if( file->e_ident[EI_CLASS] != ELFCLASS || file->e_machine != EM || file->e_ident[EI_DATA] != ELFDATA2LSB )
		return false;

	if( file->e_type != ET_DYN )
		return false;

	uint16_t phdrCount = file->e_phnum;
	Elf_Phdr *phdr = reinterpret_cast<Elf_Phdr *>( baseAddr + file->e_phoff );
	for( uint16_t i = 0; i < phdrCount; ++i )
	{
		Elf_Phdr &hdr = phdr[i];
		if( hdr.p_type == PT_LOAD && hdr.p_flags == ( PF_X | PF_R ) )
		{
			lib.memorySize = PAGE_ALIGN_UP( hdr.p_filesz );
			break;
		}
	}

	lib.baseAddress = reinterpret_cast<void *>( baseAddr );
	return true;
}
