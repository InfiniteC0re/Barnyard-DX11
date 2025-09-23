#pragma once
#include "RenderDX11.h"

namespace remaster
{

namespace dx11
{

struct GlyphMetrics
{
	TFLOAT flWidth;
	TFLOAT flHeight;
};

TFLOAT                      GetTextWidth( const TWCHAR* a_wcsText, TFLOAT a_flFontSize );
ID2D1Geometry*              CreateTextGeometry( const TWCHAR* a_wcsText, TINT a_iTextLength, TFLOAT a_flFontSize );
const GlyphMetrics*         GetLastGlyphMetrics();

}

}
