/********************************************************************
Virtual function table hook mechanism
written by LanceVorgin aka stosw aka like 90 other names
ty to qizmo for helping and uni for owning me
oh and go play in traffic trimbo - real mature eh - I blame the drugs
********************************************************************/

/*
class someinterface
{
public:
	virtual void somefunc( char *somearg ) = 0;
};

class someclass : public someinterface
{
public:
	void somefunc( char *somearg )
	{
		printf( "someclass::somefunc: %x %s\n", someval, somearg ? somearg : "NULL" );
	}

	int someval;
};


DEFVFUNC( someclass_somefunc, ( someclass *pa, char *somearg ) );

void VFUNC hookedsomefunc( someclass *pa, char *somearg )
{
	printf( "hooked it: %s\n", somearg ? somearg : "NULL" );
	someclass_somefunc( pa, "lol owned" );
	printf( "leaving hook\n" );
}

someclass q;
someclass w;

void main( )
{
	q.someval = 0xdeadbeef;
	w.someval = 0xc0defeed;

	HOOKVFUNC( &q, 0, someclass_somefunc, hookedsomefunc );
	
	dynamic_cast<someinterface *>( &q )->somefunc( "testing" ); //forces vtable lookup
	
	someclass_somefunc( &w, "should be codefeed yo" );
}
*/

#include <stdint.h>
#include "helpers.hpp"
#include "platform.hpp"

#define VTBL( classptr ) ( *(uintptr_t *)classptr )
#define PVFN_( classptr, offset ) ( VTBL( classptr ) + offset )
#define VFN_( classptr, offset ) *(uintptr_t *)PVFN_( classptr, offset )
#define PVFN( classptr, offset ) PVFN_( classptr, ( offset ) * sizeof( void * ) )
#define VFN( classptr, offset ) VFN_( classptr, ( offset ) * sizeof( void * ) )

#if defined SYSTEM_WINDOWS

#include <cstring>

extern "C" __declspec( dllimport ) int __stdcall VirtualProtect(
	void *lpAddress,
	size_t dwSize,
	unsigned long flNewProtect,
	unsigned long *lpflOldProtect
);

class CVirtualCallGate
{
public:
	CVirtualCallGate( )
	{
		Detouring::ProtectMemory( m_szGate, sizeof( m_szGate ), false );
	}

	void Build( void *pOrigFunc, void *pNewFunc, void *pOrgFuncCaller )
	{
		static uint8_t szGate[] = {
			//pop a	push c	push a	mov a, <dword>	jmp a
			0x58,	0x51,	0x50,	0xB8, 0,0,0,0,	0xFF, 0xE0,
			//pop a	pop c	push a	mov a, <dword>	jmp a
			0x58,	0x59,	0x50,	0xB8, 0,0,0,0,	0xFF, 0xE0
		};

		memcpy( m_szGate, &szGate, sizeof( szGate ) );

		*(uintptr_t *)&m_szGate[4] = (uintptr_t)pNewFunc;
		*(uintptr_t *)&m_szGate[14] = (uintptr_t)pOrigFunc;
			
		*(uintptr_t *)pOrgFuncCaller = (uintptr_t)&m_szGate[10];
	}

	uintptr_t Gate( )
	{
		return (uintptr_t)&m_szGate[0];
	}

private:
	uint8_t m_szGate[20];
};

#define VFUNC __stdcall

#define DEFVFUNC( funcname, returntype, proto ) \
	funcname##Func funcname = nullptr; \
	void *funcname##Raw_Org = nullptr; \
	CVirtualCallGate funcname##Gate
	
#define HOOKVFUNC( classptr, index, funcname, newfunc ) \
	Detouring::ProtectMemory( (void *)VTBL( classptr ), ( index + 1 ) * sizeof( void * ), false ); \
	funcname##Raw_Org = (void *)VFN( classptr, index ); \
	funcname##Gate.Build( funcname##Raw_Org, newfunc, &funcname ); \
	*(uintptr_t *)PVFN( classptr, index ) = funcname##Gate.Gate( ); \
	Detouring::ProtectMemory( (void *)VTBL( classptr ), ( index + 1 ) * sizeof( void * ), true )

#define UNHOOKVFUNC( classptr, index, funcname ) \
	Detouring::ProtectMemory( (void *)VTBL( classptr ), ( index + 1 ) * sizeof( void * ), false ); \
	*(uintptr_t *)PVFN( classptr, index ) = (uintptr_t)funcname##Raw_Org; \
	Detouring::ProtectMemory( (void *)VTBL( classptr ), ( index + 1 ) * sizeof( void * ), true )

#elif defined SYSTEM_POSIX

#define VFUNC

#define DEFVFUNC( funcname, returntype, proto ) \
	funcname##Func funcname = nullptr

#define HOOKVFUNC( classptr, index, funcname, newfunc ) \
	Detouring::ProtectMemory( (void *)VTBL( classptr ), ( index + 1 ) * sizeof( void * ), false ); \
	funcname = (funcname##Func)VFN( classptr, index ); \
	*(uintptr_t *)PVFN( classptr, index ) = (uintptr_t)newfunc; \
	Detouring::ProtectMemory( (void *)VTBL( classptr ), ( index + 1 ) * sizeof( void * ), true )

#define UNHOOKVFUNC( classptr, index, funcname  ) \
	Detouring::ProtectMemory( (void *)VTBL( classptr ), ( index + 1 ) * sizeof( void * ), false ); \
	*(uintptr_t *)PVFN( classptr, index ) = (uintptr_t)funcname; \
	Detouring::ProtectMemory( (void *)VTBL( classptr ), ( index + 1 ) * sizeof( void * ), true )

#endif

#define HDEFVFUNC( funcname, returntype, proto ) \
	typedef returntype ( VFUNC *funcname##Func ) proto; \
	extern funcname##Func funcname

#define DEFVFUNC_( funcname, returntype, proto ) \
	HDEFVFUNC( funcname, returntype, proto ); \
	DEFVFUNC( funcname, returntype, proto )
