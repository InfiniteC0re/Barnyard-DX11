#include "pch.h"
#include "Font.h"
#include "RenderDX11.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

remaster::font::Font placeholderFont;

remaster::font::Font* remaster::font::GetFont( TINT a_iIndex )
{
	placeholderFont.iID = a_iIndex;
	placeholderFont.pFontFace = remaster::g_pRender->GetDWriteFontFace();

	return &placeholderFont;
}
