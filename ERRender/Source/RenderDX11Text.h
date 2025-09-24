#pragma once
#include "RenderDX11.h"
#include "UI/Font.h"

namespace remaster
{

namespace dx11
{

ID2D1Geometry*      CreateTextGeometry( font::Font* a_pFont, const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flFontSize );
TFORCEINLINE TFLOAT ConvertDIPToPX( TFLOAT a_fDIP ) { return a_fDIP * ( 1.0f / 72.0f ) * 96.0f; }

} // namespace dx11

} // namespace remaster
