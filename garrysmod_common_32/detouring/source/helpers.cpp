/*************************************************************************
* Detouring::Helpers
* C++ helpers for detouring member functions.
*------------------------------------------------------------------------
* Copyright (c) 2017-2020, Daniel Almeida
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its
* contributors may be used to endorse or promote products derived from
* this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
* A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
* HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*************************************************************************/

#include "helpers.hpp"
#include "platform.hpp"
#include "MinHook.h"
#include <stdexcept>
#include <iostream>

#if defined SYSTEM_WINDOWS

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

#define PAGE_EXECUTE_FLAGS \
	( PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY )

#elif defined SYSTEM_LINUX

#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cinttypes>

#elif defined SYSTEM_MACOSX

#include <sys/mman.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#endif

namespace Detouring
{
	Member::Member( )
	{
		address = nullptr;
		index = static_cast<size_t>( ~0 );
	}

	Member::Member( size_t idx, void *addr )
	{
		address = addr;
		index = idx;
	}

	bool Member::IsValid( ) const
	{
		return address != nullptr;
	}

	int32_t GetMemoryProtection( void *address )
	{
		if( address == nullptr )
			return MemoryProtection::Error;

#if defined SYSTEM_WINDOWS

		MEMORY_BASIC_INFORMATION mi = { 0 };
		if( VirtualQuery( address, &mi, sizeof( mi ) ) != 0 && mi.State == MEM_COMMIT )
		{
			int32_t oldprotection = MemoryProtection::Unknown;

			if( ( mi.Protect & PAGE_NOACCESS ) != 0 )
				oldprotection = MemoryProtection::None;
			else if( ( mi.Protect & PAGE_READONLY ) != 0 )
				oldprotection = MemoryProtection::Read;
			else if( ( mi.Protect & ( PAGE_READWRITE | PAGE_WRITECOPY ) ) != 0 )
				oldprotection = MemoryProtection::Read | MemoryProtection::Write;
			else if( ( mi.Protect & PAGE_EXECUTE ) != 0 )
				oldprotection = MemoryProtection::Execute;
			else if( ( mi.Protect & PAGE_EXECUTE_READ ) != 0 )
				oldprotection = MemoryProtection::Read | MemoryProtection::Execute;
			else if( ( mi.Protect & ( PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY ) ) != 0 )
				oldprotection =
					MemoryProtection::Read | MemoryProtection::Write | MemoryProtection::Execute;

			return oldprotection;
		}

		return MemoryProtection::Error;

#elif defined SYSTEM_MACOSX

		mach_vm_address_t _address = reinterpret_cast<mach_vm_address_t>( address );
		mach_vm_size_t vmsize = 0;

#ifdef ARCHITECTURE_X86_64

		vm_region_basic_info_data_64_t info;
		mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;

#else

		vm_region_basic_info_data_t info;
		mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT;

#endif

		memory_object_name_t object = 0;

		kern_return_t status = mach_vm_region(
			mach_task_self( ),
			&_address,
			&vmsize,
			VM_REGION_BASIC_INFO,
			reinterpret_cast<vm_region_info_t>( &info ),
			&info_count,
			&object
		);

		if( status == KERN_SUCCESS )
		{
			int32_t oldprotection = MemoryProtection::None;

			if( ( info.protection & VM_PROT_READ ) != 0 )
				oldprotection |= MemoryProtection::Read;

			if( ( info.protection & VM_PROT_WRITE ) != 0 )
				oldprotection |= MemoryProtection::Write;

			if( ( info.protection & VM_PROT_EXECUTE ) != 0 )
				oldprotection |= MemoryProtection::Execute;

			return oldprotection;
		}

		return MemoryProtection::Error;

#else

		FILE *file = fopen( "/proc/self/maps", "r" );
		if( file == nullptr )
			return MemoryProtection::Error;

		uintptr_t _address = reinterpret_cast<uintptr_t>( address );
		char line[BUFSIZ] = { 0 };
		while( fgets( line, sizeof( line ), file ) != nullptr )
		{
			uint64_t start = 0, end = 0;
			char prot[5] = { 0 };
			if( sscanf( line, "%" SCNx64 "-%" SCNx64 " %4[rwxsp-]", &start, &end, prot ) == 3 &&
				start <= _address &&
				end >= _address )
			{
				fclose( file );

				int32_t oldprotection = MemoryProtection::None;

				if( prot[0] == 'r' )
					oldprotection |= MemoryProtection::Read;

				if( prot[1] == 'w' )
					oldprotection |= MemoryProtection::Write;

				if( prot[2] == 'x' )
					oldprotection |= MemoryProtection::Execute;

				return oldprotection;
			}
		}

		fclose( file );

		return MemoryProtection::Error;

#endif

	}

	bool SetMemoryProtection(
		void *address,
		size_t length,
		int32_t protection
	)
	{
		if( address == nullptr || length == 0 || protection < MemoryProtection::None )
			return false;

#if defined SYSTEM_WINDOWS

		DWORD _protection = 0;

		switch( protection )
		{
		case MemoryProtection::None:
			_protection = PAGE_NOACCESS;
			break;

		case MemoryProtection::Read:
			_protection = PAGE_READONLY;
			break;

		case MemoryProtection::Execute:
			_protection = PAGE_EXECUTE;
			break;

		case MemoryProtection::Write:
		case MemoryProtection::Read | MemoryProtection::Write:
			_protection = PAGE_READWRITE;
			break;

		case MemoryProtection::Read | MemoryProtection::Execute:
			_protection = PAGE_EXECUTE_READ;
			break;

		case MemoryProtection::Write | MemoryProtection::Execute:
		case MemoryProtection::Read | MemoryProtection::Write | MemoryProtection::Execute:
			_protection = PAGE_EXECUTE_READWRITE;
			break;

		default:
			break;
		}

		DWORD oldprotection = 0;
		return VirtualProtect( address, length, _protection, &oldprotection ) == 1;

#elif defined SYSTEM_MACOSX

		vm_prot_t _protection = 0;

		if( ( protection & MemoryProtection::Read ) != 0 )
			_protection |= VM_PROT_READ;

		if( ( protection & MemoryProtection::Write ) != 0 )
			_protection |= VM_PROT_WRITE;

		if( ( protection & MemoryProtection::Execute ) != 0 )
			_protection |= VM_PROT_EXECUTE;

		return mach_vm_protect(
			mach_task_self( ),
			reinterpret_cast<mach_vm_address_t>( address ),
			static_cast<vm_size_t>( length ),
			0,
			_protection
		) == KERN_SUCCESS;

#else

		int32_t _protection = PROT_NONE;

		if( ( protection & MemoryProtection::Read ) != 0 )
			_protection |= PROT_READ;

		if( ( protection & MemoryProtection::Write ) != 0 )
			_protection |= PROT_WRITE;

		if( ( protection & MemoryProtection::Execute ) != 0 )
			_protection |= PROT_EXEC;

		uintptr_t _address = reinterpret_cast<uintptr_t>( address ),
			diff = _address % static_cast<uintptr_t>( sysconf( _SC_PAGESIZE ) );
		address = reinterpret_cast<void *>( _address - diff );
		return mprotect( address, diff + length, _protection ) == 0;

#endif

	}

	bool ProtectMemory( void *address, size_t length, bool protect )
	{
		return SetMemoryProtection(
			address,
			length,
			protect ?
				( MemoryProtection::Read | MemoryProtection::Execute ) :
				( MemoryProtection::Read | MemoryProtection::Write | MemoryProtection::Execute )
		);
	}

	bool IsExecutableAddress( void *address )
	{
		return ( GetMemoryProtection( address ) & MemoryProtection::Execute ) != 0;
	}
}
