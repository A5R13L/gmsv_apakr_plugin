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

/*************************************************************************
* Implementation of the Detouring::GetVirtualAddress function heavily
* based on meepdarknessmeep's vhook.h header at
* https://github.com/glua/gm_fshook/blob/master/src/vhook.h
* Thanks a lot, xoxo.
*************************************************************************/

#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <tuple>

#include "platform.hpp"

namespace Detouring
{
	namespace MemoryProtection
	{
		enum : int32_t
		{
			Error = static_cast<int32_t>( 0xFFFFFFF0 ),
			Unknown = static_cast<int32_t>( 0xFFFFFFF8 ),
			None = 0x00000000,
			Read = 0x00000001,
			Write = 0x00000002,
			Execute = 0x00000004
		};
	}

	struct Member
	{
		Member( );
		Member( size_t idx, void *addr );

		bool IsValid( ) const;

		void *address;
		size_t index;
	};

	int32_t GetMemoryProtection( void *address );

	bool SetMemoryProtection( void *address, size_t length, int32_t protection );

	bool ProtectMemory( void *address, size_t length, bool protect );

	bool IsExecutableAddress( void *address );

	template<typename Class>
	inline void **GetVirtualTable( Class *instance )
	{
		return *reinterpret_cast<void ***>( instance );
	}

	template<typename Definition>
	struct FunctionTraits;

#define DETOURING_MAKE_IS_POINTER_CALLABLE( CALLING_CONVENTION, NOEXCEPT )		\
	template<typename RetType, typename... Args>								\
	struct FunctionTraits<RetType ( CALLING_CONVENTION * )( Args... ) NOEXCEPT>	\
	{																			\
		typedef RetType ( CALLING_CONVENTION *Definition )( Args... ) NOEXCEPT;	\
		static constexpr bool IsMemberFunctionPointer = false;					\
		using TargetClass = std::tuple_element_t<0, std::tuple<Args...>>;		\
		typedef RetType ReturnType;												\
		typedef std::tuple<Args...> ArgTypes;									\
	};

#define DETOURING_MAKE_IS_MEMBER_CALLABLE( CALLING_CONVENTION, CONST, NOEXCEPT )				\
	template<typename RetType, typename Class, typename... Args>								\
	struct FunctionTraits<RetType ( CALLING_CONVENTION Class::* )( Args... ) CONST NOEXCEPT>	\
	{																							\
		typedef RetType ( CALLING_CONVENTION Class::*Definition )( Args... ) CONST NOEXCEPT;	\
		static constexpr bool IsMemberFunctionPointer = true;									\
		typedef Class TargetClass;																\
		typedef RetType ReturnType;																\
		typedef std::tuple<Args...> ArgTypes;													\
	}

#define DETOURING_WITH_NOEXCEPT noexcept
#define DETOURING_WITHOUT_NOEXCEPT

#define DETOURING_WITH_CONST const
#define DETOURING_WITHOUT_CONST

#define DETOURING_MAKE_IS_CALLABLE( CALLING_CONVENTION )															\
	DETOURING_MAKE_IS_POINTER_CALLABLE( CALLING_CONVENTION, DETOURING_WITH_NOEXCEPT );								\
	DETOURING_MAKE_IS_POINTER_CALLABLE( CALLING_CONVENTION, DETOURING_WITHOUT_NOEXCEPT );							\
	DETOURING_MAKE_IS_MEMBER_CALLABLE( CALLING_CONVENTION, DETOURING_WITHOUT_CONST, DETOURING_WITH_NOEXCEPT );		\
	DETOURING_MAKE_IS_MEMBER_CALLABLE( CALLING_CONVENTION, DETOURING_WITH_CONST, DETOURING_WITH_NOEXCEPT );			\
	DETOURING_MAKE_IS_MEMBER_CALLABLE( CALLING_CONVENTION, DETOURING_WITHOUT_CONST, DETOURING_WITHOUT_NOEXCEPT );	\
	DETOURING_MAKE_IS_MEMBER_CALLABLE( CALLING_CONVENTION, DETOURING_WITH_CONST, DETOURING_WITHOUT_NOEXCEPT );

#ifdef COMPILER_VC

#ifdef ARCHITECTURE_X86

#define DETOURING_CDECL __cdecl
#define DETOURING_STDCALL __stdcall
#define DETOURING_THISCALL __thiscall
#define DETOURING_FASTCALL __fastcall

#elif defined( ARCHITECTURE_X86_64 )

#define DETOURING_CDECL

#endif

#define DETOURING_VECTORCALL __vectorcall

#elif defined( COMPILER_GNUC ) || defined( COMPILER_CLANG )

#define DETOURING_CDECL

#endif

#ifdef DETOURING_CDECL
	DETOURING_MAKE_IS_CALLABLE( DETOURING_CDECL );
#endif

#ifdef DETOURING_STDCALL
	DETOURING_MAKE_IS_CALLABLE( DETOURING_STDCALL );
#endif

#ifdef DETOURING_THISCALL
	DETOURING_MAKE_IS_CALLABLE( DETOURING_THISCALL );
#endif

#ifdef DETOURING_FASTCALL
	DETOURING_MAKE_IS_CALLABLE( DETOURING_FASTCALL );
#endif

#ifdef DETOURING_VECTORCALL
	DETOURING_MAKE_IS_CALLABLE( DETOURING_VECTORCALL );
#endif

#undef DETOURING_CDECL
#undef DETOURING_STDCALL
#undef DETOURING_THISCALL
#undef DETOURING_FASTCALL
#undef DETOURING_VECTORCALL
#undef DETOURING_WITH_NOEXCEPT
#undef DETOURING_WITHOUT_NOEXCEPT
#undef DETOURING_WITH_CONST
#undef DETOURING_WITHOUT_CONST
#undef DETOURING_MAKE_IS_POINTER_CALLABLE
#undef DETOURING_MAKE_IS_MEMBER_CALLABLE
#undef DETOURING_MAKE_IS_CALLABLE

	template<typename Definition>
	union MemberToAddress
	{
		Definition member;
		uintptr_t offset;
		void *pointer;
	};

	template<
		typename Definition,
		typename Traits = FunctionTraits<Definition>,
		std::enable_if_t<Traits::IsMemberFunctionPointer, int> = 0
	>
	inline void *GetAddress( Definition method )
	{
		MemberToAddress<Definition> magic;
		magic.member = method;
		void *address = magic.pointer;

		// Most likely a virtual table offset
		if( magic.offset <= 0xFFFF )
			return address;

		// Check whether the function starts with a relative far jump and assume a debug compilation thunk
		uint8_t *method_code = reinterpret_cast<uint8_t *>( address );
		if( method_code[0] == 0xE9 )
			address = method_code + 5 + *reinterpret_cast<int32_t *>( method_code + 1 );

		return address;
	}

	// Can be used with interfaces and implementations
	template<
		typename Definition,
		typename Traits = FunctionTraits<Definition>,
		std::enable_if_t<Traits::IsMemberFunctionPointer, int> = 0
	>
	inline Member GetVirtualAddress( void **vtable, size_t size, Definition method )
	{
		if( vtable == nullptr || size == 0 || method == nullptr )
			return Member( );

#ifdef COMPILER_VC

		void *member = GetAddress( method );
		uint8_t *addr = reinterpret_cast<uint8_t *>( member );

#ifdef ARCHITECTURE_X86_64

		// x86-64, mov rax, [rcx]
		if( addr[0] == 0x48 )
			addr += 3;

#else

		if( addr[0] == 0x8B )
			addr += 2;

#endif

		// Check for JMP functions
		if( addr[0] == 0xFF && ( ( addr[1] >> 4 ) & 3 ) == 2 )
		{
			uint8_t jumptype = addr[1] >> 6;
			uint32_t offset = 0;
			if( jumptype == 1 )
				offset = addr[2];
			else if( jumptype == 2 )
				offset = *reinterpret_cast<uint32_t *>( &addr[2] );

			size_t index = offset / sizeof( void * );
			if( index >= size )
				return Member( );

			return Member( index, vtable[index] );
		}

		for( size_t index = 0; index < size; ++index )
			if( vtable[index] == member )
				return Member( index, member );

		return Member( );

#else

		MemberToAddress<Definition> magic;
		magic.member = method;
		void *address = magic.pointer;
		const uintptr_t offset = ( magic.offset - 1 ) / sizeof( void * );

		if( offset >= size )
		{
			for( size_t index = 0; index < size; ++index )
				if( vtable[index] == address )
					return Member( index, address );

			return Member( );
		}

		return Member( offset, vtable[offset] );

#endif

	}
}
