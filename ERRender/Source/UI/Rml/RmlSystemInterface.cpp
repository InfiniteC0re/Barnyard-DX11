#include "pch.h"
#include "RmlSystemInterface.h"

#include <BYardSDK/ALocaleManager.h>
#include <BYardSDK/AUITextResolver.h>

#include <Windows.h>
#include <string>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

remaster::RmlSystemInterface::RmlSystemInterface()
{
	LARGE_INTEGER oFreq, oNow;
	QueryPerformanceFrequency( &oFreq );
	QueryPerformanceCounter( &oNow );
	m_iTicksPerSecond = oFreq.QuadPart;
	m_iStartTicks     = oNow.QuadPart;
}

double remaster::RmlSystemInterface::GetElapsedTime()
{
	LARGE_INTEGER oNow;
	QueryPerformanceCounter( &oNow );
	return double( oNow.QuadPart - m_iStartTicks ) / double( m_iTicksPerSecond );
}

static std::wstring Utf8ToWide( const Rml::String& a_rUtf8 )
{
	if ( a_rUtf8.empty() )
		return std::wstring();

	const int iLen = MultiByteToWideChar( CP_UTF8, 0, a_rUtf8.c_str(), TINT( a_rUtf8.size() ), TNULL, 0 );
	std::wstring wResult( iLen, L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, a_rUtf8.c_str(), TINT( a_rUtf8.size() ), &wResult[ 0 ], iLen );
	return wResult;
}

static Rml::String WideToUtf8( const TWCHAR* a_wcs )
{
	if ( !a_wcs || !a_wcs[ 0 ] )
		return Rml::String();

	const int iLen = WideCharToMultiByte( CP_UTF8, 0, a_wcs, -1, TNULL, 0, TNULL, TNULL );
	if ( iLen <= 1 )
		return Rml::String();

	// iLen includes the null terminator
	Rml::String sResult( iLen - 1, '\0' );
	WideCharToMultiByte( CP_UTF8, 0, a_wcs, -1, &sResult[ 0 ], iLen, TNULL, TNULL );
	return sResult;
}

// Replace each [[N]] with the game's localised string for index N
static std::wstring ReplaceLocaleMarkers( const std::wstring& a_wIn )
{
	std::wstring wResult;
	wResult.reserve( a_wIn.size() );

	const TSIZE uiLen = a_wIn.size();
	TSIZE       i     = 0;
	while ( i < uiLen )
	{
		if ( a_wIn[ i ] == L'[' && i + 1 < uiLen && a_wIn[ i + 1 ] == L'[' )
		{
			const TSIZE uiClose = a_wIn.find( L"]]", i + 2 );
			if ( uiClose != std::wstring::npos )
			{
				TINT  iIndex = 0;
				TBOOL bValid = ( uiClose > i + 2 );
				for ( TSIZE j = i + 2; j < uiClose && bValid; j++ )
				{
					const TWCHAR c = a_wIn[ j ];
					if ( c < L'0' || c > L'9' )
						bValid = TFALSE;
					else
						iIndex = iIndex * 10 + ( c - L'0' );
				}

				if ( bValid )
				{
					if ( ALocaleManager::IsSingletonCreated() )
					{
						const TWCHAR* wcsLoc = ALocaleManager::GetSingleton()->GetString( iIndex );
						if ( wcsLoc )
							wResult += wcsLoc;
					}
					i = uiClose + 2;
					continue;
				}
			}
		}

		wResult += a_wIn[ i ];
		i++;
	}

	return wResult;
}

int remaster::RmlSystemInterface::TranslateString( Rml::String& a_rTranslated, const Rml::String& a_rInput )
{
	// Only do work when a locale marker or a button-prompt token might be present
	const TBOOL bHasMarker = a_rInput.find( "[[" ) != Rml::String::npos;
	const TBOOL bHasButton = a_rInput.find( "%c" ) != Rml::String::npos;
	if ( !bHasMarker && !bHasButton )
	{
		a_rTranslated = a_rInput;
		return 0;
	}

	std::wstring wStr = Utf8ToWide( a_rInput );

	// Fetched locale strings can themselves carry button tokens, so resolve after
	if ( bHasMarker )
		wStr = ReplaceLocaleMarkers( wStr );

	TWCHAR wcsResolved[ 512 ] = {};
	AUITextResolver::ResolveActionNames( wcsResolved, TARRAYSIZE( wcsResolved ), wStr.c_str() );

	a_rTranslated = WideToUtf8( wcsResolved );
	return 1;
}

bool remaster::RmlSystemInterface::LogMessage( Rml::Log::Type a_eType, const Rml::String& a_rMessage )
{
	const TCHAR* szType = "info";
	switch ( a_eType )
	{
		case Rml::Log::LT_ERROR:
		case Rml::Log::LT_ASSERT:   szType = "error"; break;
		case Rml::Log::LT_WARNING:  szType = "warning"; break;
		default:                    break;
	}

	TUtil::Log( "[RmlUi:%s] %s\n", szType, a_rMessage.c_str() );
	return TTRUE;
}
