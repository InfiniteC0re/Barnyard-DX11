#include "pch.h"
#include "ShaderCache.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <Toshi/TArray.h>
#include <Platform/Windows/TNativeFile_Win.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static CRITICAL_SECTION s_oShaderFileCacheCriticalSection;
static volatile LONG    s_iShaderFileCacheCriticalSectionInitialized = 0;

static void EnsureShaderFileCacheCriticalSection()
{
	if ( InterlockedCompareExchange( &s_iShaderFileCacheCriticalSectionInitialized, 2, 0 ) == 0 )
	{
		InitializeCriticalSection( &s_oShaderFileCacheCriticalSection );
		InterlockedExchange( &s_iShaderFileCacheCriticalSectionInitialized, 1 );
	}
	else
	{
		while ( s_iShaderFileCacheCriticalSectionInitialized != 1 )
			Sleep( 0 );
	}
}

void remaster::dx11::LockShaderFileCache()
{
	EnsureShaderFileCacheCriticalSection();
	EnterCriticalSection( &s_oShaderFileCacheCriticalSection );
}

void remaster::dx11::UnlockShaderFileCache()
{
	LeaveCriticalSection( &s_oShaderFileCacheCriticalSection );
}

TString8 remaster::dx11::GetShaderFileDirectory( const TString8& a_rcFilepath )
{
	const TINT iSlashPos     = a_rcFilepath.FindReverse( '/' );
	const TINT iBackslashPos = a_rcFilepath.FindReverse( '\\' );
	const TINT iSeparator    = TMath::Max( iSlashPos, iBackslashPos );

	return ( iSeparator == -1 ) ? TString8() : a_rcFilepath.Mid( 0, iSeparator + 1 );
}

TString8 remaster::dx11::JoinShaderFilePath( const TString8& a_rcDirectory, const TString8& a_rcFilepath )
{
	if ( a_rcFilepath.Length() > 1 && a_rcFilepath[ 1 ] == ':' )
		return a_rcFilepath;

	if ( a_rcFilepath.Length() > 0 && ( a_rcFilepath[ 0 ] == '\\' || a_rcFilepath[ 0 ] == '/' ) )
		return a_rcFilepath;

	TString8 strResult = a_rcDirectory;
	strResult.Concat( a_rcFilepath );
	return strResult;
}

struct ShaderFileCacheEntry
{
	TString8 strFilepath;
	TCHAR*   pchData;
	TUINT    uiSize;
};

static TArray<ShaderFileCacheEntry>& GetShaderFileCacheEntries()
{
	static TArray<ShaderFileCacheEntry> s_vecCache;
	return s_vecCache;
}

TBOOL remaster::dx11::GetCachedShaderFile( const TString8& a_rcFilepath, const TCHAR*& a_rpchData, TUINT& a_ruiSize )
{
	TArray<ShaderFileCacheEntry>& vecCache = GetShaderFileCacheEntries();

	for ( TINT i = 0; i < vecCache.Size(); i++ )
	{
		if ( T2String8::CompareNoCase( vecCache[ i ].strFilepath, a_rcFilepath ) == 0 )
		{
			a_rpchData = vecCache[ i ].pchData;
			a_ruiSize  = vecCache[ i ].uiSize;
			return TTRUE;
		}
	}

	TFile* pFile = TFile::Create( a_rcFilepath );
	if ( !pFile )
		return TFALSE;

	const TSIZE uiFileSize = pFile->GetSize();
	TCHAR*      pchData    = new TCHAR[ uiFileSize + 1 ];
	const TSIZE uiReadSize = pFile->Read( pchData, uiFileSize );
	pFile->Destroy();

	if ( uiReadSize != uiFileSize )
	{
		delete[] pchData;
		return TFALSE;
	}

	pchData[ uiFileSize ] = '\0';

	ShaderFileCacheEntry oEntry;
	oEntry.strFilepath = a_rcFilepath;
	oEntry.pchData     = pchData;
	oEntry.uiSize      = TUINT( uiFileSize );
	vecCache.Push( oEntry );

	a_rpchData = pchData;
	a_ruiSize  = oEntry.uiSize;
	return TTRUE;
}

void remaster::dx11::ClearShaderFileCache()
{
	LockShaderFileCache();

	TArray<ShaderFileCacheEntry>& vecCache = GetShaderFileCacheEntries();
	for ( TINT i = 0; i < vecCache.Size(); i++ )
		delete[] vecCache[ i ].pchData;
	vecCache.Clear();

	UnlockShaderFileCache();
}

static constexpr TUINT32 SHADER_CACHE_MAGIC   = TFourCC("BYSC");
static constexpr TUINT32 SHADER_CACHE_VERSION = 1;

struct ShaderCacheHeader
{
	TUINT32 uiMagic;
	TUINT32 uiVersion;
	TUINT64 uiHash;
	TUINT32 uiNumBlobs;
};

static TUINT64 HashShaderData( const void* a_pData, TSIZE a_uiSize, TUINT64 a_uiHash )
{
	const TUINT8* pBytes = (const TUINT8*)a_pData;
	for ( TSIZE i = 0; i < a_uiSize; i++ )
	{
		a_uiHash ^= pBytes[ i ];
		a_uiHash *= 1099511628211ull;
	}
	return a_uiHash;
}

// Hashes the file and, recursively, everything it #includes (same resolution rules as
// ShaderIncludeHandler::Open); call with the file cache locked
static TBOOL HashShaderSourceTree( const TString8& a_rcFilepath, const TString8& a_rcRootDir, TUINT64& a_rHash, TINT a_iDepth )
{
	const TCHAR* pchData = TNULL;
	TUINT        uiSize  = 0;
	if ( !remaster::dx11::GetCachedShaderFile( a_rcFilepath, pchData, uiSize ) )
		return TFALSE;

	a_rHash = HashShaderData( pchData, uiSize, a_rHash );

	if ( a_iDepth >= 8 )
		return TTRUE;

	const TString8 strDir = remaster::dx11::GetShaderFileDirectory( a_rcFilepath );

	for ( TUINT i = 0; i + 8 < uiSize; i++ )
	{
		if ( T2String8::Compare( pchData + i, "#include", 8 ) != 0 )
			continue;

		TUINT uiQuote1 = i + 8;
		while ( uiQuote1 < uiSize && pchData[ uiQuote1 ] != '"' && pchData[ uiQuote1 ] != '\n' ) uiQuote1++;
		if ( uiQuote1 >= uiSize || pchData[ uiQuote1 ] != '"' )
			continue;

		TUINT uiQuote2 = uiQuote1 + 1;
		while ( uiQuote2 < uiSize && pchData[ uiQuote2 ] != '"' && pchData[ uiQuote2 ] != '\n' ) uiQuote2++;
		if ( uiQuote2 >= uiSize || pchData[ uiQuote2 ] != '"' )
			continue;

		TString8 strName;
		strName.Copy( pchData + uiQuote1 + 1, TINT( uiQuote2 - uiQuote1 - 1 ) );

		if ( !HashShaderSourceTree( remaster::dx11::JoinShaderFilePath( strDir, strName ), a_rcRootDir, a_rHash, a_iDepth + 1 ) )
			HashShaderSourceTree( remaster::dx11::JoinShaderFilePath( a_rcRootDir, strName ), a_rcRootDir, a_rHash, a_iDepth + 1 );

		i = uiQuote2;
	}

	return TTRUE;
}

TUINT64 remaster::dx11::ComputeShaderCacheHash( const TCHAR* a_pchFilepath, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const ShaderComboDefinition* a_pCombos, TUINT a_uiNumCombos, TUINT a_uiNumPermutations )
{
	TUINT64 uiHash = 14695981039346656037ull;
	uiHash         = HashShaderData( &SHADER_CACHE_VERSION, sizeof( SHADER_CACHE_VERSION ), uiHash );
	uiHash         = HashShaderData( a_pEntrypoint, T2String8::Length( a_pEntrypoint ), uiHash );
	uiHash         = HashShaderData( a_pTarget, T2String8::Length( a_pTarget ), uiHash );
	uiHash         = HashShaderData( &a_uiNumPermutations, sizeof( a_uiNumPermutations ), uiHash );

	for ( TUINT i = 0; i < a_uiNumCombos; i++ )
	{
		uiHash = HashShaderData( a_pCombos[ i ].pchName, T2String8::Length( a_pCombos[ i ].pchName ), uiHash );
		uiHash = HashShaderData( &a_pCombos[ i ].iMinValue, sizeof( TINT ) * 2 + sizeof( TUINT ), uiHash );
	}

	LockShaderFileCache();
	HashShaderSourceTree( a_pchFilepath, GetShaderFileDirectory( a_pchFilepath ), uiHash, 0 );
	UnlockShaderFileCache();

	return uiHash;
}

TString8 remaster::dx11::GetShaderCachePath( const TCHAR* a_pchFilepath, LPCSTR a_pEntrypoint, LPCSTR a_pTarget )
{
	TString8   strFilepath = a_pchFilepath;
	const TINT iSlash      = TMath::Max( strFilepath.FindReverse( '/' ), strFilepath.FindReverse( '\\' ) );
	TString8   strName     = strFilepath.Mid( iSlash + 1, strFilepath.Length() - iSlash - 1 );

	const TINT iDot = strName.FindReverse( '.' );
	if ( iDot != -1 )
		strName = strName.Mid( 0, iDot );

	return TString8::VarArgs( "Data\\ShaderCache\\%s_%s_%s.bin", strName.GetString(), a_pEntrypoint, a_pTarget );
}

TBOOL remaster::dx11::LoadShaderCacheBlobs( const TString8& a_rcCachePath, TUINT64 a_uiHash, ID3DBlob** a_ppBlobs, TUINT a_uiNumBlobs )
{
	TFile* pFile = TFile::Create( a_rcCachePath, TFILEMODE_READ );
	if ( !pFile )
		return TFALSE;

	auto fnRead = [ pFile ]( void* a_pDst, TSIZE a_uiSize ) -> TBOOL
	{
		return pFile->Read( a_pDst, a_uiSize ) == a_uiSize;
	};

	ShaderCacheHeader oHeader = {};

	TBOOL bOk = fnRead( &oHeader, sizeof( oHeader ) ) &&
	    oHeader.uiMagic == SHADER_CACHE_MAGIC &&
	    oHeader.uiVersion == SHADER_CACHE_VERSION &&
	    oHeader.uiHash == a_uiHash &&
	    oHeader.uiNumBlobs == a_uiNumBlobs;

	for ( TUINT i = 0; bOk && i < a_uiNumBlobs; i++ )
	{
		TUINT32 uiSize = 0;
		bOk            = fnRead( &uiSize, sizeof( uiSize ) ) && uiSize > 0;
		bOk            = bOk && SUCCEEDED( D3DCreateBlob( uiSize, &a_ppBlobs[ i ] ) );
		bOk            = bOk && fnRead( a_ppBlobs[ i ]->GetBufferPointer(), uiSize );
	}

	pFile->Destroy();

	if ( !bOk )
	{
		for ( TUINT i = 0; i < a_uiNumBlobs; i++ )
		{
			if ( a_ppBlobs[ i ] )
			{
				a_ppBlobs[ i ]->Release();
				a_ppBlobs[ i ] = TNULL;
			}
		}
	}

	return bOk;
}

void remaster::dx11::SaveShaderCacheBlobs( const TString8& a_rcCachePath, TUINT64 a_uiHash, ID3DBlob* const* a_ppBlobs, TUINT a_uiNumBlobs )
{
	TFileSystem* pFileSystem = TFileManager::GetSingleton()->FindFileSystem( "local" );

	if ( pFileSystem )
		TSTATICCAST( TNativeFileSystem, pFileSystem )->MakeDirectory( "Data\\ShaderCache" );

	TFile* pFile = TFile::Create( a_rcCachePath, TFILEMODE_WRITE | TFILEMODE_CREATENEW );
	if ( !pFile )
		return;

	auto fnWrite = [ pFile ]( const void* a_pSrc, TSIZE a_uiSize ) -> TBOOL
	{
		return pFile->Write( a_pSrc, a_uiSize ) == a_uiSize;
	};

	ShaderCacheHeader oHeader = { SHADER_CACHE_MAGIC, SHADER_CACHE_VERSION, a_uiHash, a_uiNumBlobs };

	TBOOL bOk = fnWrite( &oHeader, sizeof( oHeader ) );

	for ( TUINT i = 0; bOk && i < a_uiNumBlobs; i++ )
	{
		const TUINT32 uiSize = TUINT32( a_ppBlobs[ i ]->GetBufferSize() );
		bOk                  = fnWrite( &uiSize, sizeof( uiSize ) ) && fnWrite( a_ppBlobs[ i ]->GetBufferPointer(), uiSize );
	}

	pFile->Destroy();

	if ( !bOk && pFileSystem )
		pFileSystem->RemoveFile( a_rcCachePath );
}
