#include "pch.h"
#include "Viewport.h"
#include "RenderDX11.h"

#include <AHooks.h>
#include <HookHelpers.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x006d7fe0, Toshi::TViewport, TViewport_BeginSKU, void )
{
	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = m_pRenderCtx->GetViewportParameters().fX;
	viewport.TopLeftY = m_pRenderCtx->GetViewportParameters().fY;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.Width    = m_pRenderCtx->GetViewportParameters().fWidth;
	viewport.Height   = m_pRenderCtx->GetViewportParameters().fHeight;

	remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &viewport );

	if ( m_bAllowBackgroundClear )
	{
		TUINT8 r, g, b, a;
		GetBackgroundColor( r, g, b, a );
		TFLOAT clearColor[] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };

		remaster::g_pRender->GetD3D11DeviceContext()->ClearRenderTargetView( remaster::g_pRender->GetD3D11RenderTargetView(), clearColor );
	}

	if (m_bAllowDepthClear)
	{
		remaster::g_pRender->GetD3D11DeviceContext()->ClearDepthStencilView( remaster::g_pRender->GetD3D11DepthStencilView(), D3D11_CLEAR_DEPTH, 0.0f, 0 );
	}
}

void remaster::SetupRenderHooks_Viewport()
{
	InstallHook<TViewport_BeginSKU>();
}
