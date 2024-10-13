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

#pragma once

#include <string>

namespace Detouring
{
	class Hook
	{
	public:
		class Target
		{
		public:
			Target( );
			Target( void *target );
			Target( const char *target );
			Target( const std::string &target );

			bool IsValid( ) const;
			bool IsPointer( ) const;
			bool IsName( ) const;

			void *GetPointer( ) const;
			const std::string &GetName( ) const;

		protected:
			bool is_pointer = true;
			void *target_pointer = nullptr;
			std::string target_name;
		};

		class Module : public Target
		{
		public:
			Module( );
			Module( void *target );
			Module( const char *target );
			Module( const wchar_t *target );
			Module( const std::string &target );
			Module( const std::wstring &target );

			const std::wstring &GetModuleName( ) const;

		private:
			std::wstring module_name;
		};

		Hook( ) = default;
		Hook( const Target &target, void *detour );
		Hook( const Module &module, const std::string &target, void *detour );

		Hook( const Hook & ) = delete;
		Hook( Hook && ) = delete;

		~Hook( );

		Hook &operator=( const Hook & ) = delete;
		Hook &operator=( Hook && ) = delete;

		bool IsValid( ) const;

		bool Create( const Target &target, void *detour );
		bool Create( const Module &module, const std::string &target, void *detour );
		bool Destroy( );

		bool IsEnabled( ) const;
		bool Enable( );
		bool Disable( );

		void *GetTarget( ) const;

		template<typename Method>
		Method GetTarget( ) const
		{
			return reinterpret_cast<Method>( GetTarget( ) );
		}

		void *GetDetour( ) const;

		template<typename Method>
		Method GetDetour( ) const
		{
			return reinterpret_cast<Method>( GetDetour( ) );
		}

		void *GetTrampoline( ) const;

		template<typename Method>
		Method GetTrampoline( ) const
		{
			return reinterpret_cast<Method>( GetTrampoline( ) );
		}

	private:
		void *FindSymbol( const std::string &symbol );
		void *FindSymbol( void *module, const std::string &symbol );

		void *target = nullptr;
		void *detour = nullptr;
		void *trampoline = nullptr;
	};
}
