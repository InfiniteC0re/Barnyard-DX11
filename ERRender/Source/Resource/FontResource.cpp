#include "pch.h"
#include "FontResource.h"
#include "RenderDX11.h"
#include "RenderDX11Text.h"
#include "UI/GUI2Renderer.h"

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

MEMBER_HOOK( 0x006c32b0, AGUI2Font, AGUI2Font_GetTextWidth, TFLOAT, const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fScale )
{
	TFLOAT flUICanvasWidth;
	TFLOAT flUICanvasHeight;
	AGUI2::GetSingleton()->GetDimensions( flUICanvasWidth, flUICanvasHeight );
	TFLOAT flUIScaleX = ( remaster::g_pRender->GetSurfaceWidth() / flUICanvasWidth );

	// Adjust font size
	a_fScale = FONT_SCALE * a_fScale;

	return remaster::dx11::GetTextWidth( a_wszText, a_fScale ) / flUIScaleX;
}

MEMBER_HOOK( 0x006c2fe0, AGUI2Font, AGUI2Font_DrawTextSingleLine, void, const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fX, TFLOAT a_fY, TUINT32 a_uiColour, TFLOAT a_fScale, void* a_fnCallback )
{
	ID2D1Geometry* pTextGeometry = remaster::dx11::CreateTextGeometry( a_wszText, a_iTextLength, a_fScale );

	TFLOAT flUICanvasHalfWidth;
	TFLOAT flUICanvasHalfHeight;
	AGUI2::GetSingleton()->GetDimensions( flUICanvasHalfWidth, flUICanvasHalfHeight );
	flUICanvasHalfWidth *= 0.5f;
	flUICanvasHalfHeight *= 0.5f;

	auto pD2DRenderTarget = remaster::g_pRender->GetD2DRenderTarget();
	auto pD2DFactory      = remaster::g_pRender->GetD2DFactory();

	pD2DRenderTarget->BeginDraw();

	ID2D1SolidColorBrush* pBlackBrush = TNULL;
	pD2DRenderTarget->CreateSolidColorBrush( D2D1::ColorF( D2D1::ColorF::Black, ( TCOLOR_GET_A( a_uiColour ) / 255.0f ) * ( TCOLOR_GET_A( a_uiColour ) / 255.0f ) ), &pBlackBrush );

	ID2D1SolidColorBrush* pWhiteBrush = TNULL;
	pD2DRenderTarget->CreateSolidColorBrush( D2D1::ColorF( a_uiColour, TCOLOR_GET_A( a_uiColour ) / 255.0f ), &pWhiteBrush );

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

	remaster::GUI2RendererDX11* pUIRenderer = TSTATICCAST( remaster::GUI2RendererDX11, AGUI2::GetRenderer() );

	// Make sure transform is updated so we can use it
	pUIRenderer->UpdateTransform();

	D2D1_MATRIX_3X2_F matView;
	matView.m11 = pUIRenderer->GetViewMatrix().m_f11;
	matView.m12 = pUIRenderer->GetViewMatrix().m_f12;
	matView.m21 = pUIRenderer->GetViewMatrix().m_f21;
	matView.m22 = pUIRenderer->GetViewMatrix().m_f22;
	matView.dx  = pUIRenderer->GetViewMatrix().m_f41;
	matView.dy  = pUIRenderer->GetViewMatrix().m_f42;

	D2D1_MATRIX_3X2_F matProj;
	matProj.m11 = pUIRenderer->GetProjectionMatrix().m_f11;
	matProj.m12 = pUIRenderer->GetProjectionMatrix().m_f12;
	matProj.m21 = pUIRenderer->GetProjectionMatrix().m_f21;
	matProj.m22 = -pUIRenderer->GetProjectionMatrix().m_f22;
	matProj.dx  = pUIRenderer->GetProjectionMatrix().m_f41;
	matProj.dy  = pUIRenderer->GetProjectionMatrix().m_f42;

	TFLOAT flLineHeight = TFLOAT( remaster::g_pRender->GetFontMetrics().capHeight ) / remaster::g_pRender->GetFontMetrics().designUnitsPerEm / 72.0f * 96 * a_fScale * FONT_HEIGHT_FACTOR;

	auto test = D2D1::Matrix3x2F::Translation( a_fX, a_fY + flLineHeight ) * matView * matProj;

	test.m11 *= flUICanvasHalfWidth;
	test.m12 *= flUICanvasHalfHeight;
	test.m21 *= flUICanvasHalfWidth;
	test.m22 *= flUICanvasHalfHeight;
	test.dx = ( test.dx + 1.0f ) * 0.5f * remaster::g_pRender->GetSurfaceWidth();
	test.dy = ( test.dy + 1.0f ) * 0.5f * remaster::g_pRender->GetSurfaceHeight();

	pD2DRenderTarget->SetTransform( test );
	pD2DRenderTarget->DrawGeometry( pTextGeometry, pBlackBrush, ( a_fScale / 42.0f ) * 6.0f, pStrokeStyle );
	pD2DRenderTarget->FillGeometry( pTextGeometry, pWhiteBrush );

	pD2DRenderTarget->EndDraw();

	pBlackBrush->Release();
	pWhiteBrush->Release();
	pStrokeStyle->Release();
	pTextGeometry->Release();
}

MEMBER_HOOK( 0x006c3410, AGUI2Font, AGUI2Font_DrawTextWrapped, void, const TWCHAR* a_wszText, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight, TUINT32 a_uiColour, TFLOAT a_fScale, AGUI2Font::TextAlign a_eAlign, void* a_fnCallback /*= TNULL*/ )
{
	TFLOAT flUICanvasWidth;
	TFLOAT flUICanvasHeight;

	AGUI2::GetSingleton()->GetDimensions( flUICanvasWidth, flUICanvasHeight );
	TFLOAT flUIScaleX = ( remaster::g_pRender->GetSurfaceWidth() / flUICanvasWidth );
	TFLOAT flUIScaleY = ( remaster::g_pRender->GetSurfaceHeight() / flUICanvasHeight );

	// Adjust font size
	a_fScale *= FONT_SCALE;

	remaster::dx11::GetTextWidth( a_wszText, a_fScale );
	auto pTextMetrics = remaster::dx11::GetLastGlyphMetrics();

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

					fWidth1 += ( pTextMetrics[ iIndex ].flWidth * a_fScale ) / flUIScaleX;

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

			a_fY += ( pTextMetrics[ iIndex ].flHeight * a_fScale ) / flUIScaleX;

		} while ( *pTextBuffer != L'\0' );
	}
}

MEMBER_HOOK( 0x006c2e10, AGUI2Font, AGUI2Font_GetTextHeightWrapped, TFLOAT, const TWCHAR* a_wszText, TFLOAT a_fMaxWidth, TFLOAT a_fScale )
{
	TFLOAT flUICanvasWidth;
	TFLOAT flUICanvasHeight;

	AGUI2::GetSingleton()->GetDimensions( flUICanvasWidth, flUICanvasHeight );
	TFLOAT flUIScaleX = ( remaster::g_pRender->GetSurfaceWidth() / flUICanvasWidth );
	TFLOAT flUIScaleY = ( remaster::g_pRender->GetSurfaceHeight() / flUICanvasHeight );

	// Adjust font size
	a_fScale *= FONT_SCALE;

	remaster::dx11::GetTextWidth( a_wszText, a_fScale );
	auto pTextMetrics = remaster::dx11::GetLastGlyphMetrics();

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

					fWidth1 += ( pTextMetrics[ iIndex ].flWidth * a_fScale ) / flUIScaleX;

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

			fHeight += ( pTextMetrics[ iIndex ].flHeight * a_fScale ) / flUIScaleX;

		} while ( *pTextBuffer != L'\0' );

		return fHeight;
	}

	return 0.0f;
}

void remaster::SetupRenderHooks_FontResource()
{
	InstallHook<AGUI2Font_DrawTextSingleLine>();
	InstallHook<AGUI2Font_DrawTextWrapped>();
	InstallHook<AGUI2Font_GetTextWidth>();
	InstallHook<AGUI2Font_GetTextHeightWrapped>();
}
