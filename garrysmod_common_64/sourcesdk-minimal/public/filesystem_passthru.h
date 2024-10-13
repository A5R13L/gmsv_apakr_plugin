//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef FILESYSTEM_PASSTHRU_H
#define FILESYSTEM_PASSTHRU_H
#ifdef _WIN32
#pragma once
#endif


#include "filesystem.h"
#include <stdio.h>
#include <stdarg.h>

#ifdef AsyncRead
#undef AsyncRead
#undef AsyncReadMutiple
#endif

//
// These classes pass all filesystem interface calls through to another filesystem
// interface. They can be used anytime you want to override a couple things in 
// a filesystem. VMPI uses this to override the base filesystem calls while 
// allowing the rest of the filesystem functionality to work on the master.
//

template<class Base>
class CInternalFileSystemPassThru : public Base
{
public:
	CInternalFileSystemPassThru()
	{
		m_pBaseFileSystemPassThru = NULL;
	}
	virtual void InitPassThru( IBaseFileSystem *pBaseFileSystemPassThru )
	{
		m_pBaseFileSystemPassThru = pBaseFileSystemPassThru;
	}
	virtual int				Read( void* pOutput, int size, FileHandle_t file )							{ return m_pBaseFileSystemPassThru->Read( pOutput, size, file ); }
	virtual int				Write( void const* pInput, int size, FileHandle_t file )					{ return m_pBaseFileSystemPassThru->Write( pInput, size, file ); }
	virtual FileHandle_t	Open( const char *pFileName, const char *pOptions, const char *pathID )		{ return m_pBaseFileSystemPassThru->Open( pFileName, pOptions, pathID ); }
	virtual void			Close( FileHandle_t file )													{ m_pBaseFileSystemPassThru->Close( file ); }
	virtual void			Seek( FileHandle_t file, int pos, FileSystemSeek_t seekType )				{ m_pBaseFileSystemPassThru->Seek( file, pos, seekType ); }
	virtual unsigned int	Tell( FileHandle_t file )													{ return m_pBaseFileSystemPassThru->Tell( file ); }
	virtual unsigned int	Size( FileHandle_t file )													{ return m_pBaseFileSystemPassThru->Size( file ); }
	virtual unsigned int	Size( const char *pFileName, const char *pPathID )							{ return m_pBaseFileSystemPassThru->Size( pFileName, pPathID ); }
	virtual void			Flush( FileHandle_t file )													{ m_pBaseFileSystemPassThru->Flush( file ); }
	virtual bool			Precache( const char *pFileName, const char *pPathID )						{ return m_pBaseFileSystemPassThru->Precache( pFileName, pPathID ); }
	virtual bool			FileExists( const char *pFileName, const char *pPathID )					{ return m_pBaseFileSystemPassThru->FileExists( pFileName, pPathID ); }
	virtual bool			IsFileWritable( char const *pFileName, const char *pPathID )					{ return m_pBaseFileSystemPassThru->IsFileWritable( pFileName, pPathID ); }
	virtual bool			SetFileWritable( char const *pFileName, bool writable, const char *pPathID )	{ return m_pBaseFileSystemPassThru->SetFileWritable( pFileName, writable, pPathID ); }
	virtual long			GetFileTime( const char *pFileName, const char *pPathID )						{ return m_pBaseFileSystemPassThru->GetFileTime( pFileName, pPathID ); }
	virtual bool			ReadFile( const char *pFileName, const char *pPath, CUtlBuffer &buf, int nMaxBytes = 0, int nStartingByte = 0, FSAllocFunc_t pfnAlloc = NULL ) { return m_pBaseFileSystemPassThru->ReadFile( pFileName, pPath, buf, nMaxBytes, nStartingByte, pfnAlloc  ); }
	virtual bool			WriteFile( const char *pFileName, const char *pPath, CUtlBuffer &buf )		{  return m_pBaseFileSystemPassThru->WriteFile( pFileName, pPath, buf ); }
	virtual bool			UnzipFile( const char *pFileName, const char *pPath, const char *pDestination )		{  return m_pBaseFileSystemPassThru->UnzipFile( pFileName, pPath, pDestination ); }

protected:
	IBaseFileSystem *m_pBaseFileSystemPassThru;
};


class CBaseFileSystemPassThru : public CInternalFileSystemPassThru<IBaseFileSystem>
{
public:
};


class CFileSystemPassThru : public CInternalFileSystemPassThru<IFileSystem>
{
public:
	typedef CInternalFileSystemPassThru<IFileSystem> BaseClass;

	CFileSystemPassThru()
	{
		m_pFileSystemPassThru = NULL;
	}
	virtual void InitPassThru( IFileSystem *pFileSystemPassThru, bool bBaseOnly )
	{
		if ( !bBaseOnly )
			m_pFileSystemPassThru = pFileSystemPassThru;
		
		BaseClass::InitPassThru( pFileSystemPassThru );
	}

	// IAppSystem stuff.
	// Here's where the app systems get to learn about each other 
	virtual bool Connect( CreateInterfaceFn factory ) OVERRIDE													{ return m_pFileSystemPassThru->Connect( factory ); }
	virtual void Disconnect() OVERRIDE																			{ m_pFileSystemPassThru->Disconnect(); }
	virtual void *QueryInterface( const char *pInterfaceName ) OVERRIDE											{ return m_pFileSystemPassThru->QueryInterface( pInterfaceName ); }
	virtual InitReturnVal_t Init() OVERRIDE																		{ return m_pFileSystemPassThru->Init(); }
	virtual void Shutdown() OVERRIDE																			{ m_pFileSystemPassThru->Shutdown(); }

	virtual void			RemoveAllSearchPaths( void ) OVERRIDE												{ m_pFileSystemPassThru->RemoveAllSearchPaths(); }
	virtual void			AddSearchPath( const char *pPath, const char *pathID, SearchPathAdd_t addType ) OVERRIDE	{ m_pFileSystemPassThru->AddSearchPath( pPath, pathID, addType ); }
	virtual bool			RemoveSearchPath( const char *pPath, const char *pathID ) OVERRIDE					{ return m_pFileSystemPassThru->RemoveSearchPath( pPath, pathID ); }
	virtual void			RemoveFile( char const* pRelativePath, const char *pathID ) OVERRIDE				{ m_pFileSystemPassThru->RemoveFile( pRelativePath, pathID ); }
	virtual bool			RenameFile( char const *pOldPath, char const *pNewPath, const char *pathID ) OVERRIDE	{ return m_pFileSystemPassThru->RenameFile( pOldPath, pNewPath, pathID ); }
	virtual void			CreateDirHierarchy( const char *path, const char *pathID ) OVERRIDE					{ m_pFileSystemPassThru->CreateDirHierarchy( path, pathID ); }
	virtual bool			IsDirectory( const char *pFileName, const char *pathID ) OVERRIDE					{ return m_pFileSystemPassThru->IsDirectory( pFileName, pathID ); }
	virtual void			FileTimeToString( char* pStrip, int maxCharsIncludingTerminator, long fileTime ) OVERRIDE { m_pFileSystemPassThru->FileTimeToString( pStrip, maxCharsIncludingTerminator, fileTime ); }
	virtual void			SetBufferSize( FileHandle_t file, unsigned nBytes ) OVERRIDE						{ m_pFileSystemPassThru->SetBufferSize( file, nBytes  ); }
	virtual bool			IsOk( FileHandle_t file ) OVERRIDE													{ return m_pFileSystemPassThru->IsOk( file ); }
	virtual bool			EndOfFile( FileHandle_t file ) OVERRIDE												{ return m_pFileSystemPassThru->EndOfFile( file ); }
	virtual char			*ReadLine( char *pOutput, int maxChars, FileHandle_t file ) OVERRIDE				{ return m_pFileSystemPassThru->ReadLine( pOutput, maxChars, file ); }
	virtual int				FPrintf( FileHandle_t file, PRINTF_FORMAT_STRING const char *pFormat, ... ) OVERRIDE 
	{ 
		char str[8192];
		va_list marker;
		va_start( marker, pFormat );
		_vsnprintf( str, sizeof( str ), pFormat, marker );
		va_end( marker );
		return m_pFileSystemPassThru->FPrintf( file, "%s", str );
	}
	virtual CSysModule 		*LoadModule( const char *pFileName, const char *pPathID, bool bValidatedDllOnly ) OVERRIDE { return m_pFileSystemPassThru->LoadModule( pFileName, pPathID, bValidatedDllOnly ); }
	virtual void			UnloadModule( CSysModule *pModule ) OVERRIDE										{ m_pFileSystemPassThru->UnloadModule( pModule ); }
	virtual const char		*FindFirst( const char *pWildCard, FileFindHandle_t *pHandle ) OVERRIDE				{ return m_pFileSystemPassThru->FindFirst( pWildCard, pHandle ); }
	virtual const char		*FindNext( FileFindHandle_t handle ) OVERRIDE										{ return m_pFileSystemPassThru->FindNext( handle ); }
	virtual bool			FindIsDirectory( FileFindHandle_t handle ) OVERRIDE									{ return m_pFileSystemPassThru->FindIsDirectory( handle ); }
	virtual void			FindClose( FileFindHandle_t handle ) OVERRIDE										{ m_pFileSystemPassThru->FindClose( handle ); }
	virtual const char		*GetLocalPath( const char *pFileName, OUT_Z_CAP(maxLenInChars) char *pDest, int maxLenInChars ) OVERRIDE { return m_pFileSystemPassThru->GetLocalPath( pFileName, pDest, maxLenInChars ); }
	virtual bool			FullPathToRelativePath( const char *pFullpath, OUT_Z_CAP(maxLenInChars) char *pDest, int maxLenInChars ) OVERRIDE { return m_pFileSystemPassThru->FullPathToRelativePath( pFullpath, pDest, maxLenInChars ); }
	//virtual bool			GetCaseCorrectFullPath_Ptr( const char *pFullPath, OUT_Z_CAP(maxLenInChars) char *pDest, int maxLenInChars ) { return m_pFileSystemPassThru->GetCaseCorrectFullPath_Ptr( pFullPath, pDest, maxLenInChars ); }
	virtual bool			GetCurrentDirectory( char* pDirectory, int maxlen ) OVERRIDE						{ return m_pFileSystemPassThru->GetCurrentDirectory( pDirectory, maxlen ); }
	virtual void			PrintOpenedFiles( void ) OVERRIDE													{ m_pFileSystemPassThru->PrintOpenedFiles(); }
	virtual void			PrintSearchPaths( void ) OVERRIDE													{ m_pFileSystemPassThru->PrintSearchPaths(); }
	virtual void			SetWarningFunc( void (*pfnWarning)( PRINTF_FORMAT_STRING const char *fmt, ... ) ) OVERRIDE { m_pFileSystemPassThru->SetWarningFunc( pfnWarning ); }
	virtual void			SetWarningLevel( FileWarningLevel_t level ) OVERRIDE								{ m_pFileSystemPassThru->SetWarningLevel( level ); } 
	virtual void			AddLoggingFunc( void (*pfnLogFunc)( const char *fileName, const char *accessType ) ) OVERRIDE { m_pFileSystemPassThru->AddLoggingFunc( pfnLogFunc ); }
	virtual void			RemoveLoggingFunc( FileSystemLoggingFunc_t logFunc ) OVERRIDE						{ m_pFileSystemPassThru->RemoveLoggingFunc( logFunc ); }
	virtual FSAsyncStatus_t	AsyncReadMultiple( const FileAsyncRequest_t *pRequests, int nRequests, FSAsyncControl_t *pControls ) OVERRIDE { return m_pFileSystemPassThru->AsyncReadMultiple( pRequests, nRequests, pControls ); }
	virtual FSAsyncStatus_t	AsyncReadMultipleCreditAlloc( const FileAsyncRequest_t *pRequests, int nRequests, const char *pszFile, int line, FSAsyncControl_t *pControls ) OVERRIDE { return m_pFileSystemPassThru->AsyncReadMultipleCreditAlloc( pRequests, nRequests, pszFile, line, pControls ); }
	virtual FSAsyncStatus_t	AsyncFinish(FSAsyncControl_t hControl, bool wait) OVERRIDE							{ return m_pFileSystemPassThru->AsyncFinish( hControl, wait ); }
	virtual FSAsyncStatus_t	AsyncGetResult( FSAsyncControl_t hControl, void **ppData, int *pSize ) OVERRIDE		{ return m_pFileSystemPassThru->AsyncGetResult( hControl, ppData, pSize ); }
	virtual FSAsyncStatus_t	AsyncAbort(FSAsyncControl_t hControl) OVERRIDE										{ return m_pFileSystemPassThru->AsyncAbort( hControl ); }
	virtual FSAsyncStatus_t	AsyncStatus(FSAsyncControl_t hControl) OVERRIDE										{ return m_pFileSystemPassThru->AsyncStatus( hControl ); }
	virtual FSAsyncStatus_t	AsyncFlush() OVERRIDE																{ return m_pFileSystemPassThru->AsyncFlush(); }
	virtual void			AsyncAddRef( FSAsyncControl_t hControl ) OVERRIDE									{ m_pFileSystemPassThru->AsyncAddRef( hControl ); }
	virtual void			AsyncRelease( FSAsyncControl_t hControl ) OVERRIDE									{ m_pFileSystemPassThru->AsyncRelease( hControl ); }
	virtual FSAsyncStatus_t	AsyncBeginRead( const char *pszFile, FSAsyncFile_t *phFile ) OVERRIDE				{ return m_pFileSystemPassThru->AsyncBeginRead( pszFile, phFile ); }
	virtual FSAsyncStatus_t	AsyncEndRead( FSAsyncFile_t hFile ) OVERRIDE										{ return m_pFileSystemPassThru->AsyncEndRead( hFile ); }
	virtual void			AsyncAddFetcher( IAsyncFileFetch *pFetcher ) OVERRIDE								{ m_pFileSystemPassThru->AsyncAddFetcher( pFetcher ); }
	virtual void			AsyncRemoveFetcher( IAsyncFileFetch *pFetcher ) OVERRIDE							{ m_pFileSystemPassThru->AsyncRemoveFetcher( pFetcher ); }
	virtual const FileSystemStatistics *GetFilesystemStatistics() OVERRIDE										{ return m_pFileSystemPassThru->GetFilesystemStatistics(); }
	virtual WaitForResourcesHandle_t WaitForResources( const char *resourcelist ) OVERRIDE						{ return m_pFileSystemPassThru->WaitForResources( resourcelist ); }
	virtual bool			GetWaitForResourcesProgress( WaitForResourcesHandle_t handle, 
								float *progress, bool *complete ) OVERRIDE										{ return m_pFileSystemPassThru->GetWaitForResourcesProgress( handle, progress, complete ); }
	virtual void			CancelWaitForResources( WaitForResourcesHandle_t handle ) OVERRIDE					{ m_pFileSystemPassThru->CancelWaitForResources( handle ); }
	virtual int				HintResourceNeed( const char *hintlist, int forgetEverything ) OVERRIDE				{ return m_pFileSystemPassThru->HintResourceNeed( hintlist, forgetEverything ); }
	virtual bool			IsFileImmediatelyAvailable(const char *pFileName) OVERRIDE							{ return m_pFileSystemPassThru->IsFileImmediatelyAvailable( pFileName ); }
	virtual void			GetLocalCopy( const char *pFileName ) OVERRIDE										{ m_pFileSystemPassThru->GetLocalCopy( pFileName ); }
	virtual FileNameHandle_t	FindOrAddFileName( char const *pFileName ) OVERRIDE								{ return m_pFileSystemPassThru->FindOrAddFileName( pFileName ); }
	virtual FileNameHandle_t	FindFileName( char const *pFileName ) OVERRIDE									{ return m_pFileSystemPassThru->FindFileName( pFileName ); }
	virtual bool				String( const FileNameHandle_t& handle, char *buf, int buflen ) OVERRIDE		{ return m_pFileSystemPassThru->String( handle, buf, buflen ); }
	virtual bool			IsOk2( FileHandle_t file )															{ return IsOk(file); }
	virtual void			RemoveSearchPaths( const char *szPathID ) OVERRIDE									{ m_pFileSystemPassThru->RemoveSearchPaths( szPathID ); }
	virtual bool			IsSteam() const OVERRIDE															{ return m_pFileSystemPassThru->IsSteam(); }
	virtual	FilesystemMountRetval_t MountSteamContent( int nExtraAppId = -1 ) OVERRIDE							{ return m_pFileSystemPassThru->MountSteamContent( nExtraAppId ); }
	
	virtual const char		*FindFirstEx( 
		const char *pWildCard, 
		const char *pPathID,
		FileFindHandle_t *pHandle
		) OVERRIDE																								{ return m_pFileSystemPassThru->FindFirstEx( pWildCard, pPathID, pHandle ); }
	virtual void			MarkPathIDByRequestOnly( const char *pPathID, bool bRequestOnly ) OVERRIDE			{ m_pFileSystemPassThru->MarkPathIDByRequestOnly( pPathID, bRequestOnly ); }
	virtual bool			AddPackFile( const char *fullpath, const char *pathID ) OVERRIDE					{ return m_pFileSystemPassThru->AddPackFile( fullpath, pathID ); }
	virtual FSAsyncStatus_t	AsyncAppend(const char *pFileName, const void *pSrc, int nSrcBytes, bool bFreeMemory, FSAsyncControl_t *pControl ) OVERRIDE { return m_pFileSystemPassThru->AsyncAppend( pFileName, pSrc, nSrcBytes, bFreeMemory, pControl); }
	virtual FSAsyncStatus_t	AsyncWrite(const char *pFileName, const void *pSrc, int nSrcBytes, bool bFreeMemory, bool bAppend, FSAsyncControl_t *pControl ) OVERRIDE { return m_pFileSystemPassThru->AsyncWrite( pFileName, pSrc, nSrcBytes, bFreeMemory, bAppend, pControl); }
	virtual FSAsyncStatus_t	AsyncWriteFile(const char *pFileName, const CUtlBuffer *pSrc, int nSrcBytes, bool bFreeMemory, bool bAppend, FSAsyncControl_t *pControl ) OVERRIDE { return m_pFileSystemPassThru->AsyncWriteFile( pFileName, pSrc, nSrcBytes, bFreeMemory, bAppend, pControl); }
	virtual FSAsyncStatus_t	AsyncAppendFile(const char *pDestFileName, const char *pSrcFileName, FSAsyncControl_t *pControl ) OVERRIDE { return m_pFileSystemPassThru->AsyncAppendFile(pDestFileName, pSrcFileName, pControl); }
	virtual void			AsyncFinishAll( int iToPriority ) OVERRIDE											{ m_pFileSystemPassThru->AsyncFinishAll(iToPriority); }
	virtual void			AsyncFinishAllWrites() OVERRIDE														{ m_pFileSystemPassThru->AsyncFinishAllWrites(); }
	virtual FSAsyncStatus_t	AsyncSetPriority(FSAsyncControl_t hControl, int newPriority) OVERRIDE				{ return m_pFileSystemPassThru->AsyncSetPriority(hControl, newPriority); }
	virtual bool			AsyncSuspend() OVERRIDE																{ return m_pFileSystemPassThru->AsyncSuspend(); }
	virtual bool			AsyncResume() OVERRIDE																{ return m_pFileSystemPassThru->AsyncResume(); }
	virtual const char		*RelativePathToFullPath( const char *pFileName, const char *pPathID, OUT_Z_CAP(maxLenInChars) char *pDest, int maxLenInChars, PathTypeFilter_t pathFilter = FILTER_NONE, PathTypeQuery_t *pPathType = NULL ) OVERRIDE { return m_pFileSystemPassThru->RelativePathToFullPath( pFileName, pPathID, pDest, maxLenInChars, pathFilter, pPathType ); }
	virtual int				GetSearchPath( const char *pathID, bool bGetPackFiles, OUT_Z_CAP(maxLenInChars) char *pDest, int maxLenInChars ) OVERRIDE { return m_pFileSystemPassThru->GetSearchPath( pathID, bGetPackFiles, pDest, maxLenInChars ); }

	virtual FileHandle_t	OpenEx( const char *pFileName, const char *pOptions, unsigned flags = 0, const char *pathID = 0, char **ppszResolvedFilename = NULL ) OVERRIDE { return m_pFileSystemPassThru->OpenEx( pFileName, pOptions, flags, pathID, ppszResolvedFilename );}
	virtual int				ReadEx( void* pOutput, int destSize, int size, FileHandle_t file ) OVERRIDE			{ return m_pFileSystemPassThru->ReadEx( pOutput, destSize, size, file ); }
	virtual int				ReadFileEx( const char *pFileName, const char *pPath, void **ppBuf, bool bNullTerminate, bool bOptimalAlloc, int nMaxBytes = 0, int nStartingByte = 0, FSAllocFunc_t pfnAlloc = NULL ) OVERRIDE { return m_pFileSystemPassThru->ReadFileEx( pFileName, pPath, ppBuf, bNullTerminate, bOptimalAlloc, nMaxBytes, nStartingByte, pfnAlloc ); }

#if defined( TRACK_BLOCKING_IO )
	virtual void			EnableBlockingFileAccessTracking( bool state ) { m_pFileSystemPassThru->EnableBlockingFileAccessTracking( state ); }
	virtual bool			IsBlockingFileAccessEnabled() const { return m_pFileSystemPassThru->IsBlockingFileAccessEnabled(); }

	virtual IBlockingFileItemList *RetrieveBlockingFileAccessInfo() { return m_pFileSystemPassThru->RetrieveBlockingFileAccessInfo(); }
#endif
	virtual void			SetupPreloadData() OVERRIDE {}
	virtual void			DiscardPreloadData() OVERRIDE {}

	virtual void			LoadCompiledKeyValues( KeyValuesPreloadType_t type, char const *archiveFile ) OVERRIDE { m_pFileSystemPassThru->LoadCompiledKeyValues( type, archiveFile ); }

	// If the "PreloadedData" hasn't been purged, then this'll try and instance the KeyValues using the fast path of compiled keyvalues loaded during startup.
	// Otherwise, it'll just fall through to the regular KeyValues loading routines
	virtual KeyValues		*LoadKeyValues( KeyValuesPreloadType_t type, char const *filename, char const *pPathID = 0 ) OVERRIDE { return m_pFileSystemPassThru->LoadKeyValues( type, filename, pPathID ); }
	virtual bool			LoadKeyValues( KeyValues& head, KeyValuesPreloadType_t type, char const *filename, char const *pPathID = 0 ) OVERRIDE { return m_pFileSystemPassThru->LoadKeyValues( head, type, filename, pPathID ); }
	virtual bool			ExtractRootKeyName( KeyValuesPreloadType_t type, char *outbuf, size_t bufsize, char const *filename, char const *pPathID = 0 ) OVERRIDE { return m_pFileSystemPassThru->ExtractRootKeyName( type, outbuf, bufsize, filename, pPathID ); }

	virtual bool			GetFileTypeForFullPath( char const *pFullPath, wchar_t *buf, size_t bufSizeInBytes ) OVERRIDE { return m_pFileSystemPassThru->GetFileTypeForFullPath( pFullPath, buf, bufSizeInBytes ); }

	virtual bool			GetOptimalIOConstraints( FileHandle_t hFile, unsigned *pOffsetAlign, unsigned *pSizeAlign, unsigned *pBufferAlign ) OVERRIDE { return m_pFileSystemPassThru->GetOptimalIOConstraints( hFile, pOffsetAlign, pSizeAlign, pBufferAlign ); }
	virtual void			*AllocOptimalReadBuffer( FileHandle_t hFile, unsigned nSize, unsigned nOffset  ) OVERRIDE { return m_pFileSystemPassThru->AllocOptimalReadBuffer( hFile, nOffset, nSize ); }
	virtual void			FreeOptimalReadBuffer( void *p ) OVERRIDE { m_pFileSystemPassThru->FreeOptimalReadBuffer( p ); }

	virtual void			BeginMapAccess() OVERRIDE { m_pFileSystemPassThru->BeginMapAccess(); }
	virtual void			EndMapAccess() OVERRIDE { m_pFileSystemPassThru->EndMapAccess(); }

	virtual bool			ReadToBuffer( FileHandle_t hFile, CUtlBuffer &buf, int nMaxBytes = 0, FSAllocFunc_t pfnAlloc = NULL ) OVERRIDE { return m_pFileSystemPassThru->ReadToBuffer( hFile, buf, nMaxBytes, pfnAlloc ); }
	virtual bool			FullPathToRelativePathEx( const char *pFullPath, const char *pPathId, OUT_Z_CAP(maxLenInChars) char *pDest, int maxLenInChars ) OVERRIDE { return m_pFileSystemPassThru->FullPathToRelativePathEx( pFullPath, pPathId, pDest, maxLenInChars ); }
	virtual int				GetPathIndex( const FileNameHandle_t &handle ) OVERRIDE { return m_pFileSystemPassThru->GetPathIndex( handle ); }
	virtual long			GetPathTime( const char *pPath, const char *pPathID ) OVERRIDE { return m_pFileSystemPassThru->GetPathTime( pPath, pPathID ); }

	virtual DVDMode_t		GetDVDMode() OVERRIDE { return m_pFileSystemPassThru->GetDVDMode(); }

	virtual void EnableWhitelistFileTracking( bool bEnable, bool bCacheAllVPKHashes, bool bRecalculateAndCheckHashes ) OVERRIDE
		{ m_pFileSystemPassThru->EnableWhitelistFileTracking( bEnable, bCacheAllVPKHashes, bRecalculateAndCheckHashes ); }
	virtual void RegisterFileWhitelist( IPureServerWhitelist *pWhiteList, IFileList **ppFilesToReload ) OVERRIDE
		{ m_pFileSystemPassThru->RegisterFileWhitelist( pWhiteList, ppFilesToReload ); }
	virtual void MarkAllCRCsUnverified() OVERRIDE
		{ m_pFileSystemPassThru->MarkAllCRCsUnverified(); }
	virtual void CacheFileCRCs( const char *pPathname, ECacheCRCType eType, IFileList *pFilter ) OVERRIDE
		{ return m_pFileSystemPassThru->CacheFileCRCs( pPathname, eType, pFilter ); }
	virtual EFileCRCStatus CheckCachedFileHash( const char *pPathID, const char *pRelativeFilename, int nFileFraction, FileHash_t *pFileHash ) OVERRIDE
		{ return m_pFileSystemPassThru->CheckCachedFileHash( pPathID, pRelativeFilename, nFileFraction, pFileHash ); }
	virtual int GetUnverifiedFileHashes( CUnverifiedFileHash *pFiles, int nMaxFiles ) OVERRIDE
		{ return m_pFileSystemPassThru->GetUnverifiedFileHashes( pFiles, nMaxFiles ); }
	virtual int GetWhitelistSpewFlags() OVERRIDE
		{ return m_pFileSystemPassThru->GetWhitelistSpewFlags(); }
	virtual void SetWhitelistSpewFlags( int spewFlags ) OVERRIDE
		{ m_pFileSystemPassThru->SetWhitelistSpewFlags( spewFlags ); }
	virtual void InstallDirtyDiskReportFunc( FSDirtyDiskReportFunc_t func ) OVERRIDE { m_pFileSystemPassThru->InstallDirtyDiskReportFunc( func ); }

	virtual FileCacheHandle_t CreateFileCache() OVERRIDE { return m_pFileSystemPassThru->CreateFileCache(); }
	virtual void AddFilesToFileCache( FileCacheHandle_t cacheId, const char **ppFileNames, int nFileNames, const char *pPathID ) OVERRIDE { m_pFileSystemPassThru->AddFilesToFileCache( cacheId, ppFileNames, nFileNames, pPathID ); }
	virtual bool IsFileCacheFileLoaded( FileCacheHandle_t cacheId, const char *pFileName ) OVERRIDE { return m_pFileSystemPassThru->IsFileCacheFileLoaded( cacheId, pFileName ); }
	virtual bool IsFileCacheLoaded( FileCacheHandle_t cacheId ) OVERRIDE { return m_pFileSystemPassThru->IsFileCacheLoaded( cacheId ); }
	virtual void DestroyFileCache( FileCacheHandle_t cacheId ) OVERRIDE { m_pFileSystemPassThru->DestroyFileCache( cacheId ); }

	virtual bool RegisterMemoryFile( CMemoryFileBacking *pFile, CMemoryFileBacking **ppExistingFileWithRef ) OVERRIDE { return m_pFileSystemPassThru->RegisterMemoryFile( pFile, ppExistingFileWithRef ); }
	virtual void UnregisterMemoryFile( CMemoryFileBacking *pFile ) OVERRIDE { m_pFileSystemPassThru->UnregisterMemoryFile( pFile ); }
	virtual void			CacheAllVPKFileHashes( bool bCacheAllVPKHashes, bool bRecalculateAndCheckHashes ) OVERRIDE
		{ return m_pFileSystemPassThru->CacheAllVPKFileHashes( bCacheAllVPKHashes, bRecalculateAndCheckHashes ); }
	virtual bool			CheckVPKFileHash( int PackFileID, int nPackFileNumber, int nFileFraction, MD5Value_t &md5Value ) OVERRIDE
		{ return m_pFileSystemPassThru->CheckVPKFileHash( PackFileID, nPackFileNumber, nFileFraction, md5Value ); }
	virtual void			NotifyFileUnloaded( const char *pszFilename, const char *pPathId ) OVERRIDE
		{ m_pFileSystemPassThru->NotifyFileUnloaded( pszFilename, pPathId ); }


	virtual void RemoveSearchPathsByGroup( int group ) OVERRIDE { m_pFileSystemPassThru->RemoveSearchPathsByGroup( group ); }
	virtual void SetGet( IGet *get ) OVERRIDE { m_pFileSystemPassThru->SetGet( get ); }
	virtual Addon::FileSystem *Addons( ) OVERRIDE { return m_pFileSystemPassThru->Addons( ); }
	virtual Gamemode::System *Gamemodes( ) OVERRIDE { return m_pFileSystemPassThru->Gamemodes( ); }
	virtual GameDepot::System *Games( ) OVERRIDE { return m_pFileSystemPassThru->Games( ); }
	virtual LegacyAddons::System *LegacyAddons( ) OVERRIDE { return m_pFileSystemPassThru->LegacyAddons( ); }
	virtual CLanguage *Language( ) OVERRIDE { return m_pFileSystemPassThru->Language( ); }
	virtual void DoFilesystemRefresh( ) OVERRIDE { m_pFileSystemPassThru->DoFilesystemRefresh( ); }
	virtual int LastFilesystemRefresh( ) OVERRIDE { return m_pFileSystemPassThru->LastFilesystemRefresh( ); }
	virtual void AddVPKFileFromPath( const char *a, const char *b, unsigned int c ) OVERRIDE { m_pFileSystemPassThru->AddVPKFileFromPath( a, b, c ); }
	virtual void GMOD_SetupDefaultPaths( const char *a, const char *b ) OVERRIDE { m_pFileSystemPassThru->GMOD_SetupDefaultPaths( a, b ); }
	virtual void GMOD_FixPathCase( char *a, size_t b ) OVERRIDE { m_pFileSystemPassThru->GMOD_FixPathCase( a, b ); }

protected:
	IFileSystem *m_pFileSystemPassThru;
};


// This is so people who change the filesystem interface are forced to add the passthru wrapper into CFileSystemPassThru,
// so they don't break VMPI.
inline void GiveMeACompileError()
{
	CFileSystemPassThru asdf;
}


#endif // FILESYSTEM_PASSTHRU_H
