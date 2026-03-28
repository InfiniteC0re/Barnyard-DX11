#include "pch.h"
#include "GrassMaterial.h"
#include "GrassShader.h"
#include "Resource/ClassPatcher.h"
#include "RenderDX11.h"

#include <Platform/DX8/TTextureResourceHAL_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::GrassMaterial, 0x0079aafc );

extern ID3D11ShaderResourceView* g_pGrassTexture;

namespace remaster
{

GrassMaterial::GrassMaterial()
{
}

GrassMaterial::~GrassMaterial()
{
}

void GrassMaterial::PreRender()
{
	TTextureResourceHAL* pTextureHAL = TSTATICCAST( TTextureResourceHAL, m_aTextures[ 0 ] );

	if ( pTextureHAL )
	{
		pTextureHAL->Validate();

		if ( pTextureHAL->GetD3DTexture() )
		{
			g_pGrassTexture = (ID3D11ShaderResourceView*)pTextureHAL->GetD3DTexture();

			g_pRender->SetShaderResource( 0, g_pGrassTexture );
			g_pRender->PSSetSamplerState( 0, 6 );
		}
	}

	if ( m_Flags & FLAGS_NO_CULL )
		g_pRender->SetCullMode( D3D11_CULL_NONE );

	switch ( m_eBlendMode )
	{
		case 0:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
			g_pRender->SetDepthWrite( TTRUE );
			break;
		case 1:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
			g_pRender->SetDepthWrite( TFALSE );
			break;
		case 2:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_COLOR );
			g_pRender->SetDepthWrite( TFALSE );
			break;
		case 3:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE );
			g_pRender->SetDepthWrite( TFALSE );
			break;
	}

	g_pRender->SetAlphaToCoverageEnabled( TFALSE );

	//pCurrentContext->EnableFogHAL();
}

void GrassMaterial::PostRender()
{
	if ( m_Flags & FLAGS_NO_CULL )
		g_pRender->SetCullMode( D3D11_CULL_FRONT );

	g_pRender->PSSetSamplerState( 0, 3 );
	g_pRender->SetShaderResource( 0, TNULL );
	g_pRender->SetDepthWrite( TTRUE );
}

} // namespace remaster
