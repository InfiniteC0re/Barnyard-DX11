#include "pch.h"
#include "RenderDX11Text.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

constexpr TSIZE MAX_TEXT_SIZE = 4096;

static TUINT s_aCodePoints[ MAX_TEXT_SIZE ];
static TUINT16 s_aGlyphIndices[ MAX_TEXT_SIZE ];
static TFLOAT  s_aGlyphAdvances[ MAX_TEXT_SIZE ] = { 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f };

ID2D1Geometry* remaster::dx11::CreateTextGeometry( const TWCHAR* a_wcsText )
{
	TSIZE uiTextSize = TStringManager::String16Length( a_wcsText );
	if ( uiTextSize == 0 ) return TNULL;

	for ( TSIZE i = 0; i < uiTextSize; ++i )
		s_aCodePoints[ i ] = a_wcsText[ i ];

	IDWriteFontFace* pFontFace = g_pRender->GetDWriteFontFace();
	pFontFace->GetGlyphIndicesA( s_aCodePoints, uiTextSize, s_aGlyphIndices );

	// Create the path geometry
	ID2D1PathGeometry* pPathGeometry;
	g_pRender->GetD2DFactory()->CreatePathGeometry( &pPathGeometry );

	ID2D1GeometrySink* pGeometrySink;
	pPathGeometry->Open( (ID2D1GeometrySink**)&pGeometrySink );

	pFontFace->GetGlyphRunOutline(
	    ( 42 / 72.0f ) * 96.0f,
	    s_aGlyphIndices,
	    TNULL,
	    TNULL,
	    uiTextSize,
	    FALSE,
	    FALSE,
	    pGeometrySink
	);

	pGeometrySink->Close();
	pGeometrySink->Release();

	return pPathGeometry;
}
