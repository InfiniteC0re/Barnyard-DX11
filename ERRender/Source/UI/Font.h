#pragma once
#include <d2d1_3.h>
#include <dwrite_3.h>

namespace remaster
{

namespace font
{

constexpr TINT MAX_NUM_FONTS = 2;

struct Font
{
	TINT             iID;
	IDWriteFontFace* pFontFace;
};

Font* GetFont( TINT a_iIndex );

} // namespace font

} // namespace remaster
