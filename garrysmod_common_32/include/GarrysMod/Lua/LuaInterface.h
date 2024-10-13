#pragma once

#include <string>

#include "LuaBase.h"
#include "SourceCompat.h"

namespace Bootil
{
	class Buffer;
}

struct lua_Debug;
class CCommand;
class Color;

namespace GarrysMod
{
	namespace Lua
	{
		class ILuaThreadedCall;
		class ILuaGameCallback;
		class ILuaObject;

		class ILuaInterface : public ILuaBase
		{
		public:
			virtual bool Init( ILuaGameCallback *, bool ) = 0;
			virtual void Shutdown( ) = 0;
			virtual void Cycle( ) = 0;
			virtual ILuaObject *Global( ) = 0;
			virtual ILuaObject *GetObject( int index ) = 0;
			virtual void PushLuaObject( ILuaObject *obj ) = 0;
			virtual void PushLuaFunction( CFunc func ) = 0;
			virtual void LuaError( const char *err, int index ) = 0;
			virtual void TypeError( const char *name, int index ) = 0;
			virtual void CallInternal( int args, int rets ) = 0;
			virtual void CallInternalNoReturns( int args ) = 0;
			virtual bool CallInternalGetBool( int args ) = 0;
			virtual const char *CallInternalGetString( int args ) = 0;
			virtual bool CallInternalGet( int args, ILuaObject *obj ) = 0;
			virtual void NewGlobalTable( const char *name ) = 0;
			virtual ILuaObject *NewTemporaryObject( ) = 0;
			virtual bool isUserData( int index ) = 0;
			virtual ILuaObject *GetMetaTableObject( const char *name, int type ) = 0;
			virtual ILuaObject *GetMetaTableObject( int index ) = 0;
			virtual ILuaObject *GetReturn( int index ) = 0;
			virtual bool IsServer( ) = 0;
			virtual bool IsClient( ) = 0;
			virtual bool IsMenu( ) = 0;
			virtual void DestroyObject( ILuaObject *obj ) = 0;
			virtual ILuaObject *CreateObject( ) = 0;
			virtual void SetMember( ILuaObject *table, ILuaObject *key, ILuaObject *value ) = 0;
			virtual void GetNewTable( ) = 0;
			virtual void SetMember( ILuaObject *table, float key ) = 0;
			virtual void SetMember( ILuaObject *table, float key, ILuaObject *value ) = 0;
			virtual void SetMember( ILuaObject *table, const char *key ) = 0;
			virtual void SetMember( ILuaObject *table, const char *key, ILuaObject *value ) = 0;
			virtual void SetType( unsigned char ) = 0;
			virtual void PushLong( long num ) = 0;
			virtual int GetFlags( int index ) = 0;
			virtual bool FindOnObjectsMetaTable( int objIndex, int keyIndex ) = 0;
			virtual bool FindObjectOnTable( int tableIndex, int keyIndex ) = 0;
			virtual void SetMemberFast( ILuaObject *table, int keyIndex, int valueIndex ) = 0;
			virtual bool RunString( const char *filename, const char *path, const char *stringToRun, bool run, bool showErrors ) = 0;
			virtual bool IsEqual( ILuaObject *objA, ILuaObject *objB ) = 0;
			virtual void Error( const char *err ) = 0;
			virtual const char *GetStringOrError( int index ) = 0;
			virtual bool RunLuaModule( const char *name ) = 0;
			virtual bool FindAndRunScript( const char *filename, bool run, bool showErrors, const char *stringToRun, bool noReturns ) = 0;
			virtual void SetPathID( const char *pathID ) = 0;
			virtual const char *GetPathID( ) = 0;
			virtual void ErrorNoHalt( const char *fmt, ... ) = 0;
			virtual void Msg( const char *fmt, ... ) = 0;
			virtual void PushPath( const char *path ) = 0;
			virtual void PopPath( ) = 0;
			virtual const char *GetPath( ) = 0;
			virtual int GetColor( int index ) = 0;
			virtual void PushColor( Color color ) = 0;
			virtual int GetStack( int level, lua_Debug *dbg ) = 0;
			virtual int GetInfo( const char *what, lua_Debug *dbg ) = 0;
			virtual const char *GetLocal( lua_Debug *dbg, int n ) = 0;
			virtual const char *GetUpvalue( int funcIndex, int n ) = 0;
			virtual bool RunStringEx( const char *filename, const char *path, const char *stringToRun, bool run, bool printErrors, bool dontPushErrors, bool noReturns ) = 0;
			virtual size_t GetDataString( int index, const char **str ) = 0;
			virtual void ErrorFromLua( const char *fmt, ... ) = 0;
			virtual const char *GetCurrentLocation( ) = 0;
			virtual void MsgColour( const Color &col, const char *fmt, ... ) = 0;
			virtual void GetCurrentFile( std::string &outStr ) = 0;
			virtual void CompileString( Bootil::Buffer &dumper, const std::string &stringToCompile ) = 0;
			virtual bool CallFunctionProtected( int, int, bool ) = 0;
			virtual void Require( const char *name ) = 0;
			virtual const char *GetActualTypeName( int type ) = 0;
			virtual void PreCreateTable( int arrelems, int nonarrelems ) = 0;
			virtual void PushPooledString( int index ) = 0;
			virtual const char *GetPooledString( int index ) = 0;
			virtual void *AddThreadedCall( ILuaThreadedCall * ) = 0;
			virtual void AppendStackTrace( char *, unsigned long ) = 0;
			virtual void *CreateConVar( const char *, const char *, const char *, int ) = 0;
			virtual void *CreateConCommand( const char *, const char *, int, void ( * )( const CCommand & ), int ( * )( const char *, char ( * )[128] ) ) = 0;
		};

		class CLuaInterface : public ILuaInterface
		{
		public:
			inline ILuaGameCallback *GetLuaGameCallback( ) const
			{
				return gamecallback;
			}

			inline void SetLuaGameCallback( ILuaGameCallback *callback )
			{
				gamecallback = callback;
			}

		private:
			// vtable: 1 * sizeof(void **) = 4 (x86) or 8 (x86-64) bytes
			// luabase: 1 * sizeof(LuaBase *) = 4 (x86) or 8 (x86-64) bytes

			// These members represent nothing in particular
			// They've been selected to fill the required space between the vtable and the callback object
			uint64_t _1; // 8 bytes
			size_t _2[43]; // 43 * sizeof(size_t) = 172 (x86) or 344 (x86-64) bytes

#ifdef __APPLE__

			size_t _3; // 1 * sizeof(size_t) = 4 (x86) or 8 (x86-64) bytes

#endif

			// x86: offset of 188 bytes
			// x86-64: offset of 368 bytes
			// macOS adds an offset of 4 bytes (total 192) on x86 and 8 bytes (total 376) on x86-64
			ILuaGameCallback *gamecallback;
		};
	}
}