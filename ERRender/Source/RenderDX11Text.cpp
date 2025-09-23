#include "pch.h"
#include "RenderDX11Text.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

constexpr TSIZE MAX_TEXT_SIZE = 4096;

static TUINT                        s_aCodePoints[ MAX_TEXT_SIZE ];
static TUINT16                      s_aGlyphIndices[ MAX_TEXT_SIZE ];
static DWRITE_GLYPH_METRICS         s_aGlyphMetrics[ MAX_TEXT_SIZE ];
static remaster::dx11::GlyphMetrics s_aInternalGlyphMetrics[ MAX_TEXT_SIZE ];

TFLOAT remaster::dx11::GetTextWidth( const TWCHAR* a_wcsText, TFLOAT a_flFontSize )
{
	TSIZE uiTextSize = TStringManager::String16Length( a_wcsText );
	if ( uiTextSize == 0 ) return 0.0f;

	for ( TSIZE i = 0; i < uiTextSize; ++i )
		s_aCodePoints[ i ] = a_wcsText[ i ];

	IDWriteFontFace* pFontFace = g_pRender->GetDWriteFontFace();
	pFontFace->GetGlyphIndicesA( s_aCodePoints, uiTextSize, s_aGlyphIndices );
	pFontFace->GetDesignGlyphMetrics( s_aGlyphIndices, uiTextSize, s_aGlyphMetrics, TFALSE );

	TFLOAT flUnitsScale = 1.0f / g_pRender->GetFontMetrics().designUnitsPerEm;

	TFLOAT flTotalWidth = 0.0f;
	for ( size_t i = 0; i < uiTextSize; ++i )
	{
		s_aInternalGlyphMetrics[ i ].flWidth  = s_aGlyphMetrics[ i ].advanceWidth * flUnitsScale * ( 1.0f / 72.0f ) * 96;
		s_aInternalGlyphMetrics[ i ].flHeight = s_aGlyphMetrics[ i ].advanceHeight * flUnitsScale * ( 1.0f / 72.0f ) * 96;

		flTotalWidth += s_aInternalGlyphMetrics[ i ].flWidth;
	}

	return flTotalWidth * a_flFontSize;
}

ID2D1Geometry* remaster::dx11::CreateTextGeometry( const TWCHAR* a_wcsText, TINT a_iTextLength, TFLOAT a_flFontSize )
{
	if ( a_iTextLength == 0 ) return TNULL;

	for ( TSIZE i = 0; i < a_iTextLength; ++i )
		s_aCodePoints[ i ] = a_wcsText[ i ];

	IDWriteFontFace* pFontFace = g_pRender->GetDWriteFontFace();
	pFontFace->GetGlyphIndicesA( s_aCodePoints, a_iTextLength, s_aGlyphIndices );

	// Create the path geometry
	ID2D1PathGeometry* pPathGeometry;
	g_pRender->GetD2DFactory()->CreatePathGeometry( &pPathGeometry );

	ID2D1GeometrySink* pGeometrySink;
	pPathGeometry->Open( (ID2D1GeometrySink**)&pGeometrySink );

	pFontFace->GetGlyphRunOutline(
	    ( a_flFontSize / 72.0f ) * 96.0f,
	    s_aGlyphIndices,
	    TNULL,
	    TNULL,
	    a_iTextLength,
	    FALSE,
	    FALSE,
	    pGeometrySink
	);

	pGeometrySink->Close();
	pGeometrySink->Release();

	return pPathGeometry;
}

const remaster::dx11::GlyphMetrics* remaster::dx11::GetLastGlyphMetrics()
{
	return s_aInternalGlyphMetrics;
}
