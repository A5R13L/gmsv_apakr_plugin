/*************************************************************************
* MologieDetours replacement
* A C++ header that allows you to detour functions, acting as a
* replacement for MologieDetours (only implementing the simple detour
* class).
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
*
* @file detours.h
*
* @brief Declares the detours class.
*
* Basic usage:
*
* 0) Include hde.c into your project or create a library from it and statically link it.
*
* 1) Define a new type:
*
* typedef int ( *tPrintIntegers )( int, int );
*
* Make sure to specify the correct calling convention. On unixes, this is always cdecl.
* For WinAPI functions, this is always stdcall.
* To define a type with a calling convention use:
*
* typedef int ( __cdecl *tPrintIntegers )( int, int );
*
*
* 2) Create a global variable for the class instance (only required if you have to call the original function):
*
* MologieDetours::Detour<tPrintIntegers> *detour_PrintIntegers = nullptr;
*
*
* 3) Create the detour function. Its type must match the original function's type.
*
* int hook_PrintIntegers( int param1, int param2 )
* {
*     return detour_PrintIntegers->GetOriginalFunction( )( param1, param2 );
* }
*
*
* 4) Create the detour in your program's initialization routine:
*
* try
* {
*     detour_PrintIntegers = new MologieDetours::Detour<tPrintIntegers>( PrintIntegers, hook_PrintIntegers );
* }
* catch( const MologieDetours::DetourException &e )
* {
*     // Handle error
* }
*
*
* 5) Remove the detour
*
* delete detour_PrintIntegers;
*************************************************************************/

#pragma once

#include <stdint.h>
#include <string>
#include <stdexcept>
#include "MinHook.h"
#include "platform.hpp"
#include "hook.hpp"

#ifdef SYSTEM_POSIX

#include <dlfcn.h>

#endif

#define MOLOGIE_DETOURS_MEMORY_UNPROTECT( ADDRESS, SIZE, OLDPROT )
#define MOLOGIE_DETOURS_MEMORY_REPROTECT( ADDRESS, SIZE, OLDPROT )
#define MOLOGIE_DETOURS_MEMORY_WINDOWS_INIT( NAME )

#define MOLOGIE_DETOURS_DETOUR_SIZE 0

/**
* @namespace MologieDetours
*
* @brief Used to store library-specific classes.
*
* @deprecated Please port all code that uses this header to the new one.
*/
namespace MologieDetours
{

#ifdef SYSTEM_WINDOWS

	extern "C" __declspec( dllimport ) void *__stdcall GetModuleHandleA(
		const char *lpModuleName
	);

	extern "C" __declspec( dllimport ) void *__stdcall GetProcAddress(
		void *hModule,
		const char *lpProcName
	);

#endif

	/**
	* @typedef address_type
	*
	* @brief Defines an alias representing type of an address.
	*
	* @deprecated Please port all code that uses this header to the new one.
	*/
	typedef uintptr_t address_type;

	/**
	* @typedef address_pointer_type
	*
	* @brief Defines an alias representing type of a pointer to an address.
	*
	* @deprecated Please port all code that uses this header to the new one.
	*/
	typedef address_type *address_pointer_type;

	/**
	* @class DetourException
	*
	* @brief Exception for signalling detour errors.
	*
	* @deprecated Please port all code that uses this header to the new one.
	*/
	class DetourException : public std::runtime_error
	{
	public:
		explicit DetourException( const std::string &msg ) :
			std::runtime_error( msg.c_str( ) )
		{ }

		explicit DetourException( const char *msg ) :
			std::runtime_error( msg )
		{ }
	};

	/**
	* @class DetourPageProtectionException
	*
	* @brief Exception for signalling detour page protection errors.
	*
	* @deprecated Please port all code that uses this header to the new one.
	*/
	class DetourPageProtectionException : public DetourException
	{
	public:
		explicit DetourPageProtectionException( const std::string &msg, const void *addr ) :
			DetourException( msg.c_str( ) ),
			errorAddress( addr )
		{ }

		explicit DetourPageProtectionException( const char *msg, const void *addr ) :
			DetourException( msg ),
			errorAddress( addr )
		{ }

		const void *GetErrorAddress( )
		{
			return errorAddress;
		}

	private:
		const void *errorAddress;
	};

	/**
	* @class DetourDisassemblerException
	*
	* @brief Exception for signalling detour disassembler errors.
	*
	* @deprecated Please port all code that uses this header to the new one.
	*/
	class DetourDisassemblerException : public DetourException
	{
	public:
		explicit DetourDisassemblerException( const std::string &msg ) :
			DetourException( msg.c_str( ) )
		{ }

		explicit DetourDisassemblerException( const char *msg ) :
			DetourException( msg )
		{ }
	};

	/**
	* @class DetourRelocationException
	*
	* @brief Exception for signalling detour relocation errors.
	*
	* @deprecated Since this uses the MinHook library, it is impossible to know what caused the error.
	* Please port all code that uses this header to the new one.
	*/
	class DetourRelocationException : public DetourException
	{
	public:
		explicit DetourRelocationException( const std::string &msg ) :
			DetourException( msg.c_str( ) )
		{ }

		explicit DetourRelocationException( const char *msg ) :
			DetourException( msg )
		{ }
	};

	/**
	* @class Detour
	*
	* @brief Used for creating detours using detour trampolines.
	*
	* @deprecated Please port all code that uses this header to the new one.
	*/
	template<typename function_type>
	class Detour
	{
	public:
		/**
		* @fn Detour::Detour( function_type pSource, function_type pDetour )
		*
		* @brief Creates a new local detour using a given function address.
		*
		* @param pSource The source function.
		* @param pDetour The detour function.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		Detour( function_type pSource, function_type pDetour ) :
			target( pSource ),
			detour( pDetour )
		{
			CreateDetour( );
		}

		/**
		* @fn Detour::Detour( function_type pSource, function_type pDetour, size_t instructionCount )
		*
		* @brief Creates a new local detour using a given function address and a predefined
		* instruction count.
		*
		* @param pSource The source function.
		* @param pDetour The detour function.
		* @param instructionCount Size of instructions to replace, must be >= MOLOGIE_DETOURS_DETOUR_SIZE.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		Detour( function_type pSource, function_type pDetour, size_t instructionCount ) :
			target( pSource ),
			detour( pDetour )
		{
			(void)instructionCount;
			CreateDetour( );
		}

#ifdef SYSTEM_WINDOWS

		/**
		* @fn Detour::Detour( const char *moduleName, const char *lpProcName, function_type pDetour )
		*
		* @brief Creates a new local detour on an exported function.
		*
		* @param moduleName The Name of the module.
		* @param lpProcName Name of the pointer to a proc.
		* @param pDetour The detour.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		Detour( const char *moduleName, const char *lpProcName, function_type pDetour ) :
			target( reinterpret_cast<function_type>(
				GetProcAddress( GetModuleHandleA( moduleName ), lpProcName ) )
			),
			detour( pDetour )
		{
			CreateDetour( );
		}
		/**
		* @fn Detour::Detour( void *module, const char *lpProcName, function_type pDetour )
		*
		* @brief Creates a new local detour on an exported function.
		*
		* @param module The module.
		* @param lpProcName Name of the pointer to a proc.
		* @param pDetour The detour.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		Detour( void *module, const char *lpProcName, function_type pDetour ) :
			target( reinterpret_cast<function_type>( GetProcAddress( module, lpProcName ) ) ),
			detour( pDetour )
		{
			CreateDetour( );
		}

#else

		/**
		* @fn Detour::Detour( const char *moduleName, const char *lpProcName, function_type pDetour )
		*
		* @brief Creates a new local detour on an exported function.
		*
		* @param moduleName The Name of the module.
		* @param lpProcName Name of the pointer to a proc.
		* @param pDetour The detour.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		Detour( const char *moduleName, const char *lpProcName, function_type pDetour ) :
			target( nullptr ),
			detour( pDetour )
		{
			void *binary = dlopen( moduleName, RTLD_LOCAL | RTLD_NOLOAD );
			target = reinterpret_cast<function_type>( dlsym( binary, lpProcName ) );
			dlclose( binary );

			CreateDetour( );
		}

		/**
		* @fn Detour::Detour( void *module, const char *lpProcName, function_type pDetour )
		*
		* @brief Creates a new local detour on an exported function.
		*
		* @param module The module.
		* @param lpProcName Name of the pointer to a proc.
		* @param pDetour The detour.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		Detour( void *module, const char *lpProcName, function_type pDetour ) :
			target( reinterpret_cast<function_type>( dlsym( module, lpProcName ) ) ),
			detour( pDetour )
		{
			CreateDetour( );
		}

#endif

		/**
		* @fn Detour::~Detour( )
		*
		* @brief Destroys the detour.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		~Detour( )
		{
			try
			{
				Revert( );
			}
			catch( const DetourException & )
			{ }
		}

		/**
		* @fn size_t Detour::GetInstructionCount( )
		*
		* @brief Gets the size of the code replaced.
		*
		* @return Always returns 0.
		*
		* @deprecated Since this uses the MinHook library, it is impossible to retrieve the code size.
		* Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		size_t GetInstructionCount( )
		{
			return 0;
		}

		/**
		* @fn function_type Detour::GetSource( )
		*
		* @brief Gets the source.
		*
		* @return Returns the address of the detoured target function.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		function_type GetSource( )
		{
			return target;
		}

		/**
		* @fn function_type Detour::GetDetour( )
		*
		* @brief Gets the detour.
		*
		* @return Returns the address of the detour.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		function_type GetDetour( )
		{
			return detour;
		}

		/**
		* @fn function_type Detour::GetOriginalFunction( )
		*
		* @brief Gets the original function.
		*
		* @return Returns a function pointer which can be used to execute the original function.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		function_type GetOriginalFunction( )
		{
			return trampoline;
		}

	private:
		/**
		* @fn void Detour::CreateDetour( )
		*
		* @brief Creates the detour.
		*
		* @exception DetourException Thrown when MinHook returns an error.
		* @exception DetourDisassemblerException Thrown when the disassembler returns an error or
		* an unexpected result.
		* @exception DetourPageProtectionException Thrown when the page protection of detour-related
		* memory can not be changed.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		void CreateDetour( )
		{
			MH_Initialize( );

			switch( MH_CreateHook(
				reinterpret_cast<void *>( target ),
				reinterpret_cast<void *>( detour ),
				reinterpret_cast<void **>( &trampoline )
			) )
			{
			case MH_OK:
				break;

			case MH_ERROR_NOT_INITIALIZED:
				throw DetourException( "MinHook library was not initialized" );

			case MH_ERROR_ALREADY_CREATED:
				throw DetourException( "Hook was already created" );

			case MH_ERROR_NOT_EXECUTABLE:
				throw DetourException( "Address doesn't have an executable flag" );

			case MH_ERROR_UNSUPPORTED_FUNCTION:
				throw DetourDisassemblerException( "Unable to detour function" );

			case MH_ERROR_MEMORY_ALLOC:
				throw DetourException( "Unable to allocate memory for hook" );

			case MH_ERROR_MEMORY_PROTECT:
				throw DetourPageProtectionException(
					"Failed to change page protection of original function",
					reinterpret_cast<void *>( target )
				);

			default:
				throw DetourException( "Unknown error returned by MH_CreateHook" );
			}

			switch( MH_EnableHook( reinterpret_cast<void *>( target ) ) )
			{
			case MH_OK:
				break;

			case MH_ERROR_NOT_INITIALIZED:
				throw DetourException( "MinHook library was not initialized" );

			case MH_ERROR_NOT_CREATED:
				throw DetourException( "Hook was not created" );

			case MH_ERROR_ENABLED:
				throw DetourException( "Hook was already enabled" );

			case MH_ERROR_MEMORY_PROTECT:
				throw DetourPageProtectionException(
					"Failed to change page protection of original function",
					reinterpret_cast<void *>( target )
				);

			default:
				throw DetourException( "Unknown error returned by MH_EnableHook" );
			}
		}

		/**
		* @fn void Detour::Revert( )
		*
		* @brief Reverts any changes made and restores the original code.
		*
		* @exception DetourException Thrown when MinHook returns an error.
		* @exception DetourPageProtectionException Thrown when the target function's page protection
		* can't be changed.
		*
		* @deprecated Please port all code that uses this header to the new one.
		*/
		void Revert( )
		{
			switch( MH_RemoveHook( reinterpret_cast<void *>( target ) ) )
			{
			case MH_OK:
				break;

			case MH_ERROR_NOT_INITIALIZED:
				throw DetourException( "MinHook library was not initialized" );

			case MH_ERROR_NOT_CREATED:
				throw DetourException( "Hook was not created" );

			case MH_ERROR_MEMORY_PROTECT:
				throw DetourPageProtectionException(
					"Failed to change page protection of original function",
					reinterpret_cast<void *>( target )
				);

			default:
				throw DetourException( "Unknown error returned by MH_RemoveHook" );
			}

			MH_Uninitialize( );
		}

		function_type target; // Pointer to target function
		function_type detour; // Pointer to detour function
		function_type trampoline; // Pointer to the trampoline
	};

	/**
	* @class DetourImport
	*
	* @brief Used for creating detours on an import of a single module.
	*
	* @deprecated This class is a dummy.
	*/
	template<typename function_type>
	class DetourImport
	{
	public:
		/**
		* @fn DetourImport::DetourImport( address_type, function_type )
		*
		* @brief Creates a new local detour using a given import.
		*
		* @deprecated This function is a dummy.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		DetourImport( uintptr_t, function_type )
		{ }

		/**
		* @fn bool DetourImport::IsValid( )
		*
		* @brief Query if the detour is still applied.
		*
		* @return Always returns false.
		*
		* @deprecated This function is a dummy.
		*/
		DEPRECATED_WITH_SUBSTITUTE( Detouring::Hook )
		bool IsValid( )
		{
			return false;
		}
	};
}
