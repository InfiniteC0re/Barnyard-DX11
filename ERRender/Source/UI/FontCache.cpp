#include "pch.h"
#include "FontCache.h"
#include "RenderDX11.h"
#include "RenderDX11Text.h"
#include "Hash.h"

#include <Toshi/T2Map.h>
#include <Toshi/TScheduler.h>
#include <Toshi/TColor.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

struct SolidColorBrush
{
	ID2D1SolidColorBrush* pBrush;
	TFLOAT                flLastUsedTime;
};

static Toshi::T2Map<TUINT32, SolidColorBrush> s_mapBrushes;
static Toshi::T2Map<TUINT32, SolidColorBrush> s_mapFontsIndices;

static remaster::fontcache::GlyphMetrics s_aGlyphMetrics[ 65535 ];
static TUINT16                           s_aGlyphIndices[ 65535 ];

ID2D1SolidColorBrush* remaster::fontcache::GetSolidColorBrush( TUINT32 a_uiColor )
{
	TPROFILER_SCOPE();
	const TFLOAT flCurrentTime = g_oSystemManager.GetScheduler()->GetTotalTime();

	auto it = s_mapBrushes.Find( a_uiColor );
	if ( it != s_mapBrushes.End() )
	{
		it->second.flLastUsedTime = flCurrentTime;
		return it->second.pBrush;
	}

	// Create new brush
	auto pD2DRenderTarget = remaster::g_pRender->GetD2DRenderTarget();

	ID2D1SolidColorBrush* pBrush = TNULL;
	pD2DRenderTarget->CreateSolidColorBrush( D2D1::ColorF( a_uiColor, ( TCOLOR_GET_A( a_uiColor ) / 255.0f ) * ( TCOLOR_GET_A( a_uiColor ) / 255.0f ) ), &pBrush );

	s_mapBrushes.Insert( a_uiColor, { pBrush, flCurrentTime } );
	return pBrush;
}

//static Toshi::T2Map<TUINT64, TFLOAT>          s_mapTextLengths[ remaster::font::MAX_NUM_FONTS ];

TFLOAT remaster::fontcache::GetTextWidth( font::Font* a_pFont, const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flFontSize )
{
	TPROFILER_SCOPE();

	// NOTE: calculating width is simple now, so caching is disabled

#if 0

	const TUINT64 uiHash = hash_64_fnv1a( a_wcsText, a_uiTextLength );

	// Check if width is in the cache
	auto& rMap = s_mapTextLengths[ a_pFont->iID ];
	auto it = rMap.Find( uiHash );
	if ( it != rMap.End() ) return it->second * a_flFontSize;

	// Calculate width and store in the cache
	TFLOAT flWidth = dx11::CalculateTextWidth( a_pFont, a_wcsText, a_uiTextLength, a_flFontSize );
	rMap.Insert( uiHash, flWidth / a_flFontSize );

	return flWidth;

#else
	
	TFLOAT flTotalWidth = 0.0f;
	for ( TSIZE i = 0; i < a_uiTextLength; ++i )
		flTotalWidth += s_aGlyphMetrics[ a_wcsText[ i ] ].flWidth;

	return flTotalWidth * a_flFontSize;

#endif
	
}

TFLOAT remaster::fontcache::GetTextHeight( font::Font* a_pFont, const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flFontSize )
{
	TFLOAT flMaxHeight = 0.0f;
	for ( TSIZE i = 0; i < a_uiTextLength; ++i )
		flMaxHeight = TMath::Max( flMaxHeight, s_aGlyphMetrics[ a_wcsText[ i ] ].flHeight );

	return flMaxHeight * a_flFontSize;
}

const remaster::fontcache::GlyphMetrics* remaster::fontcache::GetGlyphMetrics()
{
	return s_aGlyphMetrics;
}

const TUINT16* remaster::fontcache::GetGlyphIndices()
{
	return s_aGlyphIndices;
}

void remaster::fontcache::Create()
{
	TPROFILER_SCOPE();
	auto pGlyphMetrics = new DWRITE_GLYPH_METRICS[ 65535 ];
	auto pCharacters   = new TUINT32[ 65535 ];

	for ( TUINT32 i = 0; i < 65535; i++ )
		pCharacters[ i ] = i;

	IDWriteFontFace* pFontFace = g_pRender->GetDWriteFontFace();
	pFontFace->GetGlyphIndicesA( pCharacters, 65535, s_aGlyphIndices );
	pFontFace->GetDesignGlyphMetrics( s_aGlyphIndices, 65535, pGlyphMetrics, TFALSE );

	TFLOAT flUnitsScale = dx11::ConvertDIPToPX( 1.0f / g_pRender->GetFontMetrics().designUnitsPerEm );

	for ( TSIZE i = 0; i < 65535; ++i )
	{
		s_aGlyphMetrics[ i ].flWidth  = pGlyphMetrics[ i ].advanceWidth * flUnitsScale;
		s_aGlyphMetrics[ i ].flHeight = pGlyphMetrics[ i ].verticalOriginY * flUnitsScale;
		s_aGlyphMetrics[ i ].flHeight += ( ( pGlyphMetrics[ i ].topSideBearing < 0 ? -pGlyphMetrics[ i ].topSideBearing : 0 ) + ( pGlyphMetrics[ i ].bottomSideBearing < 0 ? -pGlyphMetrics[ i ].bottomSideBearing : 0 ) ) * flUnitsScale;
	}

	delete[] pCharacters;
	delete[] pGlyphMetrics;
}

void remaster::fontcache::Destroy()
{
	// Destroy all dynamically created brushes
	T2_FOREACH( s_mapBrushes, it )
		it->GetSecond().pBrush->Release();

	s_mapBrushes.Clear();
}

constexpr TFLOAT GARBAGE_COLLECTION_INTERVAL  = 5.0f;
constexpr TFLOAT GARBAGE_COLLECTION_THRESHOLD = 10.0f;
static TFLOAT    s_flUntilGarbageCollection   = GARBAGE_COLLECTION_INTERVAL;

void remaster::fontcache::Update()
{
	TPROFILER_SCOPE();
	const TFLOAT flCurrentTime = g_oSystemManager.GetScheduler()->GetTotalTime();

	if ( flCurrentTime > s_flUntilGarbageCollection )
	{
		// Destroy unused brushes
		for ( auto it = ( s_mapBrushes ).Begin(); it != ( s_mapBrushes ).End(); )
		{
			auto next = it.Next();

			if ( flCurrentTime - it->second.flLastUsedTime >= GARBAGE_COLLECTION_THRESHOLD )
			{
				it->GetSecond().pBrush->Release();
				s_mapBrushes.Remove( it );
			}

			it = next;
		}

		// Update interval time
		s_flUntilGarbageCollection = flCurrentTime + GARBAGE_COLLECTION_INTERVAL;
	}
}
