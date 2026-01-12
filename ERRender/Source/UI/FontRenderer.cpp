#include "pch.h"
#include "FontRenderer.h"
#include "RenderDX11.h"
#include "UIRenderer.h"
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

static remaster::RenderDX11::FONT GetFontIndex( AGUI2Font* a_pFont )
{
	static AGUI2Font* s_pRekord26 = CALL( 0x006c44d0, AGUI2Font*, const TCHAR*, "Rekord26" );
	static AGUI2Font* s_pRekord18 = CALL( 0x006c44d0, AGUI2Font*, const TCHAR*, "Rekord18" );

	if ( s_pRekord26 == a_pFont ) return remaster::RenderDX11::FONT_REKORD26;
	if ( s_pRekord18 == a_pFont ) return remaster::RenderDX11::FONT_REKORD18;

	TASSERT( !"Should never happen" );
	return remaster::RenderDX11::FONT_REKORD26;
}

MEMBER_HOOK( 0x006c32b0, AGUI2Font, AGUI2Font_GetTextWidth, TFLOAT, const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fScale )
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
		return CallOriginal( a_wszText, a_iTextLength, a_fScale );

	return remaster::g_pRender->GetFontAtlas( GetFontIndex( this ) )->GetTextWidth( a_wszText, a_iTextLength, a_fScale );
}

MEMBER_HOOK( 0x006c2fe0, AGUI2Font, AGUI2Font_DrawTextSingleLine, void, const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fX, TFLOAT a_fY, TUINT32 a_uiColour, TFLOAT a_fScale, void* a_fnCallback )
{
	TPROFILER_SCOPE();
	if ( !remaster::fontrenderer::IsHDEnabled() )
	{
		CallOriginal( a_wszText, a_iTextLength, a_fX, a_fY, a_uiColour, a_fScale, a_fnCallback );
		return;
	}

	remaster::FontAtlas*      pFontAtlas         = remaster::g_pRender->GetFontAtlas( GetFontIndex( this ) );
	ID3D11ShaderResourceView* pFontAtlasResource = pFontAtlas->GetTextureResource();

	remaster::g_pUIRender->SetMaterial( TNULL );
	remaster::g_pUIRender->SetColour( a_uiColour );
	remaster::g_pUIRender->SetTextureResourceView( pFontAtlasResource );
	remaster::g_pUIRender->SetShaderType( remaster::UIRendererDX11::ST_FONT );

	TFLOAT flScaleXFactor = 1.0f / remaster::g_pUIRender->GetScaleX();
	TFLOAT flScaleYFactor = 1.0f / remaster::g_pUIRender->GetScaleY();

	TFLOAT fX = a_fX;
	TFLOAT fY = a_fY - pFontAtlas->GetLineHeightOffset() - pFontAtlas->GetSDFMarginSize() * 0.5f;

	static remaster::FontAtlas::CharInfo s_aCharInfo[ 512 ];

	// Calculate max height to offset the text
	TFLOAT flMaxHeight = 0.0f;
	for ( TINT i = 0; i < a_iTextLength; i++ )
	{
		pFontAtlas->GetCharUV( a_wszText[ i ], a_fScale, s_aCharInfo[ i ] );
		flMaxHeight = TMath::Max( flMaxHeight, ( s_aCharInfo[ i ].flHeight - ( s_aCharInfo[ i ].flHeight - s_aCharInfo[ i ].iBearingY ) ) * flScaleYFactor );
	}

	// Draw text
	for ( TINT i = 0; i < a_iTextLength; i++ )
	{
		remaster::FontAtlas::CharInfo& rCharInfo = s_aCharInfo[ i ];

		TFLOAT flWidth  = rCharInfo.flWidth * flScaleXFactor;
		TFLOAT flHeight = rCharInfo.flHeight * flScaleYFactor;

		TFLOAT flGlyphX = fX + rCharInfo.iBearingX * flScaleXFactor;
		TFLOAT flGlyphY = fY + ( rCharInfo.flHeight - rCharInfo.iBearingY ) * flScaleYFactor + flMaxHeight;

		remaster::g_pUIRender->RenderRectangle(
		    { flGlyphX, flGlyphY - flHeight },
		    { flGlyphX + flWidth, flGlyphY },
		    { rCharInfo.flUV1X, rCharInfo.flUV1Y },
		    { rCharInfo.flUV2X, rCharInfo.flUV2Y }
		);

		fX += ( rCharInfo.iAdvanceX >> 6 ) * flScaleXFactor;
	}
}

MEMBER_HOOK( 0x006c3410, AGUI2Font, AGUI2Font_DrawTextWrapped, void, const TWCHAR* a_wszText, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight, TUINT32 a_uiColour, TFLOAT a_fScale, AGUI2Font::TextAlign a_eAlign, void* a_fnCallback /*= TNULL*/ )
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
	{
		CallOriginal( a_wszText, a_fX, a_fY, a_fWidth, a_fHeight, a_uiColour, a_fScale, a_eAlign, a_fnCallback );
		return;
	}

	remaster::FontAtlas* pFontAtlas = remaster::g_pRender->GetFontAtlas( GetFontIndex( this ) );
	TFLOAT               flUIScaleX = remaster::g_pUIRender->GetScaleX();
	TFLOAT               flUIScaleY = remaster::g_pUIRender->GetScaleY();

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

					fWidth1 += ( oCharInfo.iAdvanceX >> 6 ) / flUIScaleX;
					flMaxCharHeight = TMath::Max( flMaxCharHeight, oCharInfo.flHeight );

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

	remaster::FontAtlas* pFontAtlas = remaster::g_pRender->GetFontAtlas( GetFontIndex( this ) );
	TFLOAT               flUIScaleX = remaster::g_pUIRender->GetScaleX();
	TFLOAT               flUIScaleY = remaster::g_pUIRender->GetScaleY();

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

					fWidth1 += ( oCharInfo.iAdvanceX >> 6 ) / flUIScaleX;
					flMaxCharHeight = TMath::Max( flMaxCharHeight, oCharInfo.flHeight );

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

		return ( fHeight - pFontAtlas->GetHeightOffset() - pFontAtlas->GetSDFMarginSize() ) / flUIScaleY;
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
}

void remaster::fontrenderer::Destroy()
{
}

void remaster::fontrenderer::Update()
{
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
