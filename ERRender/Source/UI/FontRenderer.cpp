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

constexpr TFLOAT FONT_SCALE         = 38.0f;
constexpr TFLOAT FONT_HEIGHT_FACTOR = 0.65f;

static ID2D1StrokeStyle*                      s_pStrokeStyle;

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

	TFLOAT flUICanvasWidth;
	TFLOAT flUICanvasHeight;
	AGUI2::GetSingleton()->GetDimensions( flUICanvasWidth, flUICanvasHeight );
	TFLOAT flUIScaleX = ( remaster::g_pRender->GetSurfaceWidth() / flUICanvasWidth );

	// Adjust font size
	a_fScale = FONT_SCALE * a_fScale;

	return remaster::fontcache::GetTextWidth( remaster::font::GetFont( 0 ), a_wszText, a_iTextLength, a_fScale ) / flUIScaleX;
}

MEMBER_HOOK( 0x006c2fe0, AGUI2Font, AGUI2Font_DrawTextSingleLine, void, const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fX, TFLOAT a_fY, TUINT32 a_uiColour, TFLOAT a_fScale, void* a_fnCallback )
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
	{
		CallOriginal( a_wszText, a_iTextLength, a_fX, a_fY, a_uiColour, a_fScale, a_fnCallback );
		return;
	}

	auto pUIRenderer      = remaster::g_pUIRender;
	auto pD2DRenderTarget = remaster::g_pRender->GetD2DRenderTarget();
	auto pD2DFactory      = remaster::g_pRender->GetD2DFactory();

	// Make sure transform is updated, so we can use it to render text
	pUIRenderer->UpdateTransform();

	// Get view matrix from the AGUI2Renderer
	D2D1_MATRIX_3X2_F matView;
	matView.m11 = pUIRenderer->GetViewMatrix().m_f11;
	matView.m12 = -pUIRenderer->GetViewMatrix().m_f12;
	matView.m21 = pUIRenderer->GetViewMatrix().m_f21;
	matView.m22 = -pUIRenderer->GetViewMatrix().m_f22;
	matView.dx  = pUIRenderer->GetViewMatrix().m_f41;
	matView.dy  = -pUIRenderer->GetViewMatrix().m_f42;

	TFLOAT flLineHeight = TFLOAT( remaster::g_pRender->GetFontMetrics().capHeight ) / remaster::g_pRender->GetFontMetrics().designUnitsPerEm / 72.0f * 96 * a_fScale * FONT_HEIGHT_FACTOR;

	TFLOAT flScaleXFactor = 1.0f / pUIRenderer->GetScaleX();
	TFLOAT flScaleYFactor = 1.0f / pUIRenderer->GetScaleY();

	// Calculate transform
	auto transform = D2D1::Matrix3x2F::Translation( a_fX, a_fY + flLineHeight ) * matView;
	transform.m11 *= flScaleXFactor;
	transform.m12 *= flScaleYFactor;
	transform.m21 *= flScaleXFactor;
	transform.m22 *= flScaleYFactor;
	transform.dx = GetViewportX() + ( transform.dx + GetViewportWidth() * 0.5f );
	transform.dy = GetViewportY() + ( transform.dy + GetViewportHeight() * 0.5f );

	// Initialise scissors rectangle
	D2D1_RECT_F oClipRect;
	oClipRect.left   = GetViewportX();
	oClipRect.top    = GetViewportY();
	oClipRect.right  = oClipRect.left + GetViewportWidth();
	oClipRect.bottom = oClipRect.top + GetViewportHeight();

	// Draw the text
	ID2D1Geometry*        pTextGeometry = remaster::dx11::CreateTextGeometry( remaster::font::GetFont( 0 ), a_wszText, a_iTextLength, a_fScale );
	ID2D1SolidColorBrush* pOutlineBrush = remaster::fontcache::GetSolidColorBrush( TCOLOR4( 0, 0, 0, TCOLOR_GET_A( a_uiColour ) ) );
	ID2D1SolidColorBrush* pColorBrush   = remaster::fontcache::GetSolidColorBrush( a_uiColour );

	pD2DRenderTarget->BeginDraw();
	pD2DRenderTarget->SetTransform( D2D1::Matrix3x2F::Identity() );
	pD2DRenderTarget->PushAxisAlignedClip( &oClipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE );

	pD2DRenderTarget->SetTransform( transform );
	pD2DRenderTarget->DrawGeometry( pTextGeometry, pOutlineBrush, a_fScale * ( 1.0f / 42.0f ) * 6.0f, s_pStrokeStyle );
	pD2DRenderTarget->FillGeometry( pTextGeometry, pColorBrush );
	pD2DRenderTarget->PopAxisAlignedClip();
	pD2DRenderTarget->EndDraw();

	pTextGeometry->Release();
}

MEMBER_HOOK( 0x006c3410, AGUI2Font, AGUI2Font_DrawTextWrapped, void, const TWCHAR* a_wszText, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight, TUINT32 a_uiColour, TFLOAT a_fScale, AGUI2Font::TextAlign a_eAlign, void* a_fnCallback /*= TNULL*/ )
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
	{
		CallOriginal( a_wszText, a_fX, a_fY, a_fWidth, a_fHeight, a_uiColour, a_fScale, a_eAlign, a_fnCallback );
		return;
	}

	TFLOAT flUICanvasWidth;
	TFLOAT flUICanvasHeight;

	AGUI2::GetSingleton()->GetDimensions( flUICanvasWidth, flUICanvasHeight );
	TFLOAT flUIScaleX = ( remaster::g_pRender->GetSurfaceWidth() / flUICanvasWidth );
	TFLOAT flUIScaleY = ( remaster::g_pRender->GetSurfaceHeight() / flUICanvasHeight );

	// Adjust font size
	a_fScale *= FONT_SCALE;

	auto pTextMetrics = remaster::fontcache::GetGlyphMetrics();

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

					fWidth1 += ( pTextMetrics[ wChar ].flWidth * a_fScale ) / flUIScaleX;

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

				( ( void( __thiscall* )( AGUI2Font*, const TWCHAR*, TINT, TFLOAT, TFLOAT, TUINT32, TFLOAT, void* ) )( 0x006c2fe0 ) )( this, pTextBuffer2, pTextBuffer - pTextBuffer2, fPosX, a_fY, a_uiColour, a_fScale, a_fnCallback );
			}

			a_fY += ( pTextMetrics[ a_wszText[ iIndex ] ].flHeight * a_fScale ) / flUIScaleX;

		} while ( *pTextBuffer != L'\0' );
	}
}

MEMBER_HOOK( 0x006c2e10, AGUI2Font, AGUI2Font_GetTextHeightWrapped, TFLOAT, const TWCHAR* a_wszText, TFLOAT a_fMaxWidth, TFLOAT a_fScale )
{
	if ( !remaster::fontrenderer::IsHDEnabled() )
		return CallOriginal( a_wszText, a_fMaxWidth, a_fScale );

	TFLOAT flUICanvasWidth;
	TFLOAT flUICanvasHeight;

	AGUI2::GetSingleton()->GetDimensions( flUICanvasWidth, flUICanvasHeight );
	TFLOAT flUIScaleX = ( remaster::g_pRender->GetSurfaceWidth() / flUICanvasWidth );
	TFLOAT flUIScaleY = ( remaster::g_pRender->GetSurfaceHeight() / flUICanvasHeight );

	// Adjust font size
	a_fScale *= FONT_SCALE;

	auto pTextMetrics = remaster::fontcache::GetGlyphMetrics();

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

					fWidth1 += ( pTextMetrics[ wChar ].flWidth * a_fScale ) / flUIScaleX;

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

			fHeight += ( pTextMetrics[ a_wszText[ iIndex ] ].flHeight * a_fScale ) / flUIScaleX;

		} while ( *pTextBuffer != L'\0' );

		return fHeight;
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

	auto pD2DRenderTarget = remaster::g_pRender->GetD2DRenderTarget();
	auto pD2DFactory      = remaster::g_pRender->GetD2DFactory();

	ID2D1StrokeStyle* pStrokeStyle;
	pD2DFactory->CreateStrokeStyle(
	    D2D1::StrokeStyleProperties(
	        D2D1_CAP_STYLE_ROUND,
	        D2D1_CAP_STYLE_ROUND,
	        D2D1_CAP_STYLE_ROUND,
	        D2D1_LINE_JOIN_ROUND,
	        0.0f,
	        D2D1_DASH_STYLE_SOLID,
	        0.0f
	    ),
	    TNULL,
	    0,
	    &pStrokeStyle
	);

	remaster::fontcache::Create();
}

void remaster::fontrenderer::Destroy()
{
	fontcache::Destroy();

	// Destroy main objects
	if ( s_pStrokeStyle ) s_pStrokeStyle->Release();
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
