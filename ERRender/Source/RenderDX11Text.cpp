#include "pch.h"
#include "RenderDX11Text.h"
#include "UI/FontCache.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

constexpr TSIZE MAX_TEXT_SIZE = 4096;
static TUINT16  s_aTextIndices[ MAX_TEXT_SIZE ];

ID2D1Geometry* remaster::dx11::CreateTextGeometry( font::Font* a_pFont, const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flFontSize )
{
	if ( a_uiTextLength == 0 ) return TNULL;

	auto pIndices = remaster::fontcache::GetGlyphIndices();
	for ( TSIZE i = 0; i < a_uiTextLength; ++i )
		s_aTextIndices[ i ] = pIndices[ a_wcsText[ i ] ];

	// Create the path geometry
	ID2D1PathGeometry* pPathGeometry;
	g_pRender->GetD2DFactory()->CreatePathGeometry( &pPathGeometry );

	ID2D1GeometrySink* pGeometrySink;
	pPathGeometry->Open( (ID2D1GeometrySink**)&pGeometrySink );

	a_pFont->pFontFace->GetGlyphRunOutline(
	    ConvertDIPToPX( a_flFontSize ),
	    s_aTextIndices,
	    TNULL,
	    TNULL,
	    a_uiTextLength,
	    FALSE,
	    FALSE,
	    pGeometrySink
	);

	pGeometrySink->Close();
	pGeometrySink->Release();

	return pPathGeometry;
}

