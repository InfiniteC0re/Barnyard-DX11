#include "pch.h"
#include "FontRenderer.h"
#include "RenderDX11.h"
#include "RenderDX11Text.h"
#include "UIRenderer.h"
#include "FontCache.h"
#include "Font.h"

#include <Toshi/TScheduler.h>
#include <Toshi/T2Map.h>

#include <AHooks.h>
#include <HookHelpers.h>

#include <BYardSDK/AGUI2.h>
#include <BYardSDK/AGUI2Font.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static TFLOAT GetViewportX()
{
	return TSTATICCAST( remaster::UIRendererDX11, AGUI2::GetRenderer() )->GetViewportX();
}

static TFLOAT GetViewportY()
{
	return TSTATICCAST( remaster::UIRendererDX11, AGUI2::GetRenderer() )->GetViewportY();
}

static TFLOAT GetViewportWidth()
{
	return TSTATICCAST( remaster::UIRendererDX11, AGUI2::GetRenderer() )->GetViewportWidth();
}

static TFLOAT GetViewportHeight()
{
	return TSTATICCAST( remaster::UIRendererDX11, AGUI2::GetRenderer() )->GetViewportHeight();
}

MEMBER_HOOK( 0x006c32b0, AGUI2Font, AGUI2Font_GetTextWidth, TFLOAT, const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fScale )
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
		return CallOriginal( a_wszText, a_iTextLength, a_fScale );

	return remaster::g_pRender->GetFontAtlas()->GetTextWidth( a_wszText, a_iTextLength, a_fScale );
}

MEMBER_HOOK( 0x006c2fe0, AGUI2Font, AGUI2Font_DrawTextSingleLine, void, const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fX, TFLOAT a_fY, TUINT32 a_uiColour, TFLOAT a_fScale, void* a_fnCallback )
{
	TPROFILER_SCOPE();
	if ( !remaster::fontrenderer::IsHDEnabled() )
	{
		CallOriginal( a_wszText, a_iTextLength, a_fX, a_fY, a_uiColour, a_fScale, a_fnCallback );
		return;
	}

	remaster::FontAtlas*      pFontAtlas         = remaster::g_pRender->GetFontAtlas();
	ID3D11ShaderResourceView* pFontAtlasResource = pFontAtlas->GetTextureResource();

	remaster::g_pUIRender->SetMaterial( TNULL );
	remaster::g_pUIRender->SetColour( a_uiColour );
	remaster::g_pUIRender->SetTextureResourceView( pFontAtlasResource );
	remaster::g_pUIRender->SetPixelShader();

	TFLOAT flScaleXFactor = 1.0f / remaster::g_pUIRender->GetScaleX();
	TFLOAT flScaleYFactor = 1.0f / remaster::g_pUIRender->GetScaleY();
	TFLOAT flSpriteMargin = pFontAtlas->GetSpriteMargin();
	TFLOAT flOutline      = pFontAtlas->GetOutlineSize( a_fScale );
	TFLOAT flHalfOutline  = 0.5f * flOutline;

	TFLOAT fX = a_fX - flHalfOutline - flSpriteMargin * 0.5f;
	TFLOAT fY = a_fY - pFontAtlas->GetHeightOffset() - flHalfOutline - flSpriteMargin * 0.5f;

	remaster::FontAtlas::CharInfo oCharInfo;
	for ( TINT i = 0; i < a_iTextLength; i++ )
	{
		pFontAtlas->GetCharUV( a_wszText[ i ], a_fScale, oCharInfo );

		TFLOAT flWidth  = oCharInfo.flWidth * flScaleXFactor;
		TFLOAT flHeight = oCharInfo.flHeight * flScaleYFactor;

		remaster::g_pUIRender->RenderRectangle(
		    { fX, fY },
		    { fX + flWidth, fY + flHeight },
		    { oCharInfo.flUV1X, oCharInfo.flUV1Y },
		    { oCharInfo.flUV2X, oCharInfo.flUV2Y }
		);

		fX += flWidth - flOutline;
	}
}

MEMBER_HOOK( 0x006c3410, AGUI2Font, AGUI2Font_DrawTextWrapped, void, const TWCHAR* a_wszText, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight, TUINT32 a_uiColour, TFLOAT a_fScale, AGUI2Font::TextAlign a_eAlign, void* a_fnCallback /*= TNULL*/ )
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
	{
		CallOriginal( a_wszText, a_fX, a_fY, a_fWidth, a_fHeight, a_uiColour, a_fScale, a_eAlign, a_fnCallback );
		return;
	}

	auto                 pTextMetrics   = remaster::fontcache::GetGlyphMetrics();
	remaster::FontAtlas* pFontAtlas     = remaster::g_pRender->GetFontAtlas();
	TFLOAT               flUIScaleX     = remaster::g_pUIRender->GetScaleX();
	TFLOAT               flUIScaleY     = remaster::g_pUIRender->GetScaleY();
	TFLOAT               flSpriteMargin = pFontAtlas->GetSpriteMargin();
	TFLOAT               flOutlineSize  = pFontAtlas->GetOutlineSize( a_fScale );

	if ( a_wszText && a_wszText[ 0 ] != '\0' )
	{
		auto pTextBuffer = a_wszText;

		do
		{
			auto pTextBuffer2 = pTextBuffer;

			while ( *pTextBuffer2 != L'\0' && iswspace( *pTextBuffer2 ) != 0 )
			{
				if ( *pTextBuffer2 == L'\n' ) break;
				pTextBuffer2++;
			}

			auto wChar  = *pTextBuffer2;
			TINT iIndex = pTextBuffer2 - a_wszText;

			TFLOAT flMaxCharHeight = 0.0f;

			if ( wChar == L'\n' )
			{
				pTextBuffer = pTextBuffer2 + 1;
			}
			else
			{
				if ( wChar == L'\0' ) return;

				pTextBuffer         = TNULL;
				TFLOAT fWidth1      = 0.0f;
				TFLOAT fWidth2      = 0.0f;
				auto   pTextBuffer3 = pTextBuffer2;

				do {
					if ( wChar == L'\n' )
					{
						fWidth2     = fWidth1;
						pTextBuffer = pTextBuffer3;
						break;
					}

					remaster::FontAtlas::CharInfo oCharInfo;
					pFontAtlas->GetCharUV( wChar, a_fScale, oCharInfo );

					fWidth1 += ( oCharInfo.flWidth ) / flUIScaleX - flOutlineSize - flSpriteMargin * 2 / flUIScaleY;
					flMaxCharHeight = TMath::Max( flMaxCharHeight, oCharInfo.flHeight - flOutlineSize * flUIScaleY - flSpriteMargin * 2 * flUIScaleY );

					if ( iswspace( wChar ) != 0 && *pTextBuffer3 != L'\xA0' )
					{
						fWidth2     = fWidth1;
						pTextBuffer = pTextBuffer3;
					}

					wChar = pTextBuffer3[ 1 ];
					if ( wChar == L'\0' ) break;
					pTextBuffer3++;
					iIndex++;

				} while ( fWidth1 < a_fWidth || pTextBuffer == TNULL );

				if ( pTextBuffer3[ 1 ] == L'\0' )
				{
					pTextBuffer = pTextBuffer3 + 1;
					fWidth2     = fWidth1;
				}

				TFLOAT fPosX;

				if ( a_eAlign == AGUI2Font::TextAlign_Left ) fPosX = a_fX;
				else if ( a_eAlign == AGUI2Font::TextAlign_Center ) fPosX = ( a_fWidth - fWidth2 ) * 0.5f + a_fX;
				else if ( a_eAlign == AGUI2Font::TextAlign_Right ) fPosX = ( a_fWidth - fWidth2 ) + a_fX;
				else fPosX = a_fX;

				TREINTERPRETCAST( AGUI2Font_DrawTextSingleLine::_hook_obj*, this )->_hook_func( pTextBuffer2, pTextBuffer - pTextBuffer2, fPosX, a_fY, a_uiColour, a_fScale, a_fnCallback );
			}

			a_fY += flMaxCharHeight / flUIScaleY;

		} while ( *pTextBuffer != L'\0' );
	}
}

MEMBER_HOOK( 0x006c2e10, AGUI2Font, AGUI2Font_GetTextHeightWrapped, TFLOAT, const TWCHAR* a_wszText, TFLOAT a_fMaxWidth, TFLOAT a_fScale )
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
		return CallOriginal( a_wszText, a_fMaxWidth, a_fScale );

	auto                 pTextMetrics   = remaster::fontcache::GetGlyphMetrics();
	remaster::FontAtlas* pFontAtlas     = remaster::g_pRender->GetFontAtlas();
	TFLOAT               flUIScaleX     = remaster::g_pUIRender->GetScaleX();
	TFLOAT               flUIScaleY     = remaster::g_pUIRender->GetScaleY();
	TFLOAT               flSpriteMargin = pFontAtlas->GetSpriteMargin();
	TFLOAT               flOutlineSize  = pFontAtlas->GetOutlineSize( a_fScale );

	if ( a_wszText && a_wszText[ 0 ] != '\0' )
	{
		TFLOAT fHeight     = 0.0f;
		auto   pTextBuffer = a_wszText;

		do
		{
			auto pTextBuffer2 = pTextBuffer;

			while ( *pTextBuffer2 != L'\0' && iswspace( *pTextBuffer2 ) != 0 )
			{
				if ( *pTextBuffer2 == L'\n' ) break;
				pTextBuffer2++;
			}

			auto wChar  = *pTextBuffer2;
			TINT iIndex = pTextBuffer2 - a_wszText;

			TFLOAT flMaxCharHeight = 0.0f;

			if ( wChar == L'\n' )
			{
				pTextBuffer = pTextBuffer2 + 1;
			}
			else
			{
				if ( wChar == L'\0' ) break;

				pTextBuffer         = TNULL;
				TFLOAT fWidth1      = 0.0f;
				TFLOAT fWidth2      = 0.0f;
				auto   pTextBuffer3 = pTextBuffer2;

				do {
					if ( wChar == L'\n' )
					{
						fWidth2     = fWidth1;
						pTextBuffer = pTextBuffer3;
						break;
					}

					remaster::FontAtlas::CharInfo oCharInfo;
					pFontAtlas->GetCharUV( wChar, a_fScale, oCharInfo );

					fWidth1 += ( oCharInfo.flWidth ) / flUIScaleX - flOutlineSize - flSpriteMargin * 2;
					flMaxCharHeight = TMath::Max( flMaxCharHeight, oCharInfo.flHeight - flOutlineSize * flUIScaleY - flSpriteMargin * 2 * flUIScaleY );

					if ( iswspace( wChar ) != 0 && *pTextBuffer3 != L'\xA0' )
					{
						fWidth2     = fWidth1;
						pTextBuffer = pTextBuffer3;
					}

					wChar = pTextBuffer3[ 1 ];
					if ( wChar == L'\0' ) break;
					pTextBuffer3++;
					iIndex++;

				} while ( fWidth1 < a_fMaxWidth || pTextBuffer == TNULL );

				if ( pTextBuffer3[ 1 ] == L'\0' )
				{
					pTextBuffer = pTextBuffer3 + 1;
					fWidth2     = fWidth1;
				}
			}

			fHeight += flMaxCharHeight;

		} while ( *pTextBuffer != L'\0' );

		return ( fHeight - pFontAtlas->GetHeightOffset() ) / flUIScaleY;
	}

	return 0.0f;
}

void remaster::SetupRenderHooks_FontRenderer()
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
		return;

	InstallHook<AGUI2Font_DrawTextSingleLine>();
	InstallHook<AGUI2Font_DrawTextWrapped>();
	InstallHook<AGUI2Font_GetTextWidth>();
	InstallHook<AGUI2Font_GetTextHeightWrapped>();
}

void remaster::fontrenderer::Create()
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
		return;

	remaster::fontcache::Create();
}

void remaster::fontrenderer::Destroy()
{
	fontcache::Destroy();
}

void remaster::fontrenderer::Update()
{
	fontcache::Update();
}

static TBOOL s_bHDEnabled = TTRUE;

void remaster::fontrenderer::SetHDEnabled( TBOOL a_bEnabled )
{
	s_bHDEnabled = a_bEnabled;
}

TBOOL remaster::fontrenderer::IsHDEnabled()
{
	return s_bHDEnabled;
}
