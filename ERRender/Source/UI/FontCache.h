#pragma once
#include "Font.h"

namespace remaster
{

namespace fontcache
{

struct GlyphMetrics
{
	TFLOAT flWidth;
	TFLOAT flHeight;
};

ID2D1SolidColorBrush* GetSolidColorBrush( TUINT32 a_uiColor );
TFLOAT                GetTextWidth( font::Font* a_pFont, const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flFontSize );
const GlyphMetrics*   GetGlyphMetrics();
const TUINT16*        GetGlyphIndices();


void Create();
void Destroy();
void Update();

} // namespace fontcache

} // namespace remaster
