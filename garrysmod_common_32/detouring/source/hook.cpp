/*************************************************************************
* Detouring::Hook
* A C++ class that allows you to detour functions.
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

#include "hook.hpp"
#include "helpers.hpp"
#include "platform.hpp"
#include "MinHook.h"

#include <cstring>

#if defined SYSTEM_WINDOWS

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <Psapi.h>
#include <vector>

#elif defined SYSTEM_POSIX

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <dlfcn.h>

#endif

namespace Detouring
{
	Hook::Target::Target( ) { }

	Hook::Target::Target( void *target ) : target_pointer( target ) { }

	Hook::Target::Target( const char *target ) :
		is_pointer( false ), target_name( target, target + std::strlen( target ) ) { }

	Hook::Target::Target( const std::string &target ) :
		is_pointer( false ), target_name( target.begin( ), target.end( ) ) { }

	bool Hook::Target::IsValid( ) const
	{
		return !is_pointer || target_pointer != nullptr;
	}

	bool Hook::Target::IsPointer( ) const
	{
		return is_pointer && target_pointer != nullptr;
	}

	bool Hook::Target::IsName( ) const
	{
		return !is_pointer;
	}

	void *Hook::Target::GetPointer( ) const
	{
		return target_pointer;
	}

	const std::string &Hook::Target::GetName( ) const
	{
		return target_name;
	}

	Hook::Module::Module( ) { }

	Hook::Module::Module( void *target ) : Target( target ) { }

	Hook::Module::Module( const char *target ) :
		Target( target ), module_name( target_name.begin( ), target_name.end( ) ) { }

	Hook::Module::Module( const wchar_t *target ) : module_name( target )
	{
		is_pointer = false;
	}

	Hook::Module::Module( const std::string &target ) :
		Target( target ), module_name( target.begin( ), target.end( ) ) { }

	Hook::Module::Module( const std::wstring &target ) : module_name( target )
	{
		is_pointer = false;
	}

	const std::wstring &Hook::Module::GetModuleName( ) const
	{
		return module_name;
	}

	Hook::Hook( const Target &_target, void *_detour )
	{
		Create( _target, _detour );
	}

	Hook::Hook( const Module &module, const std::string &_target, void *_detour )
	{
		Create( module, _target, _detour );
	}

	Hook::~Hook( )
	{
		Destroy( );
	}

	bool Hook::IsValid( ) const
	{
		return target != nullptr && detour != nullptr;
	}

	bool Hook::Create( const Target &_target, void *_detour )
	{
		if( !_target.IsValid( ) || _detour == nullptr )
			return false;

		void *pointer = nullptr;
		if( _target.IsPointer( ) )
			pointer = _target.GetPointer( );
		else
			pointer = FindSymbol( _target.GetName( ) );

		if( pointer == nullptr )
			return false;

		MH_Initialize( );

		if( MH_CreateHook( pointer, _detour, &trampoline ) == MH_OK )
		{
			target = pointer;
			detour = _detour;
			return true;
		}

		return false;
	}

	bool Hook::Create( const Module &module, const std::string &_target, void *_detour )
	{
		if( !module.IsValid( ) || _target.empty( ) )
			return false;

		if( module.IsPointer( ) )
		{
			void *pointer = FindSymbol( module.GetPointer( ), _target.c_str( ) );
			return pointer != nullptr ? Create( pointer, _detour ) : false;
		}

		if( _detour == nullptr )
			return false;

		MH_Initialize( );

		if( MH_CreateHookApiEx( module.GetModuleName( ).c_str( ), _target.c_str( ), _detour, &trampoline, &target ) == MH_OK )
		{
			detour = _detour;
			return true;
		}

		return false;
	}

	bool Hook::Destroy( )
	{
		if( target == nullptr )
			return false;

		if( MH_RemoveHook( target ) != MH_OK )
			return false;

		target = nullptr;
		detour = nullptr;
		trampoline = nullptr;
		MH_Uninitialize( );
		return true;
	}

	bool Hook::IsEnabled( ) const
	{
		return IsValid( ) && MH_IsHookEnabled( target ) == MH_HOOK_ENABLED;
	}

	bool Hook::Enable( )
	{
		return IsValid( ) && MH_EnableHook( target ) == MH_OK;
	}

	bool Hook::Disable( )
	{
		return IsValid( ) && MH_DisableHook( target ) == MH_OK;
	}

	void *Hook::GetTarget( ) const
	{
		return target;
	}

	void *Hook::GetDetour( ) const
	{
		return detour;
	}

	void *Hook::GetTrampoline( ) const
	{
		return trampoline;
	}

	void *Hook::FindSymbol( const std::string &symbol )
	{

#if defined SYSTEM_WINDOWS

		std::vector<HMODULE> modules( 256 );
		DWORD size = static_cast<DWORD>( modules.size( ) * sizeof( HMODULE ) );
		DWORD needed = 0;
		if( !EnumProcessModules( GetCurrentProcess( ), modules.data( ), size, &needed ) )
			return nullptr;

		if( needed > size )
		{
			modules.resize( needed / sizeof( HMODULE ) );
			size = needed;
			needed = 0;
			if( !EnumProcessModules( GetCurrentProcess( ), modules.data( ), size, &needed ) )
				return nullptr;
		}

		for( HMODULE module : modules )
		{
			void *pointer = FindSymbol( module, symbol );
			if( pointer != nullptr )
				return pointer;
		}

		return nullptr;

#elif defined SYSTEM_POSIX

		return dlsym( RTLD_DEFAULT, symbol.c_str( ) );

#endif

	}

	void *Hook::FindSymbol( void *module, const std::string &symbol )
	{

#if defined SYSTEM_WINDOWS

		return reinterpret_cast<void *>( GetProcAddress(
			reinterpret_cast<HMODULE>( module ), symbol.c_str( )
		) );

#elif defined SYSTEM_POSIX

		return dlsym( module, symbol.c_str( ) );

#endif

	}
}
