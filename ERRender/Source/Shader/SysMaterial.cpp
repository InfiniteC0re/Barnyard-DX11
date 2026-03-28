#include "pch.h"
#include "SysMaterial.h"
#include "Resource/ClassPatcher.h"
#include "SysShader.h"
#include "RenderDX11.h"

#include <Platform/DX8/TTextureResourceHAL_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::SysMaterial, 0x0079a498 );

remaster::SysMaterial::SysMaterial()
    : m_pAssignedOrderTable( TNULL )
{
}

remaster::SysMaterial::~SysMaterial()
{
}

void remaster::SysMaterial::PreRender()
{
	if ( m_pTexture )
	{
		auto pTexture = TSTATICCAST( TTextureResourceHAL, m_pTexture );
		pTexture->Validate();

		auto pD3DTexture = (ID3D11ShaderResourceView*)pTexture->GetD3DTexture();

		g_pRender->SetShaderResource( 0, pD3DTexture );

		if ( pTexture->GetAddressUState() == ADDRESSINGMODE_CLAMP && pTexture->GetAddressVState() == ADDRESSINGMODE_CLAMP )
			g_pRender->PSSetSamplerState( 0, 1 );
		else
			g_pRender->PSSetSamplerState( 0, 3 );
	}
	else
	{
		g_pRender->SetShaderResource( 0, TNULL );
	}

	if ( m_Flags & FLAGS_NO_CULL )
	{
		g_pRender->SetCullMode( D3D11_CULL_NONE );
	}

	switch ( m_eBlendMode )
	{
		case BLENDMODE_DEFAULT:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
			g_pRender->SetDepthWrite( TTRUE );
			break;
		case BLENDMODE_1:
		case BLENDMODE_8:
		case BLENDMODE_9:
		case BLENDMODE_10:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
			g_pRender->SetDepthWrite( TFALSE );
			break;
		case BLENDMODE_2:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR );
			g_pRender->SetDepthWrite( TFALSE );
			break;
		case BLENDMODE_3:
		case BLENDMODE_6:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE );
			g_pRender->SetDepthWrite( TFALSE );
			break;
		case BLENDMODE_7:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE );
			g_pRender->SetDepthWrite( TFALSE );
			g_pRender->SetDepthEnabled( TFALSE );
			break;
		default:
			g_pRender->SetBlendMode( g_pRender->IsBlendEnabled(), g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
			g_pRender->SetDepthWrite( TTRUE );
			break;
	}
}

void remaster::SysMaterial::PostRender()
{
	if ( m_Flags & FLAGS_NO_CULL )
	{
		g_pRender->SetCullMode( D3D11_CULL_BACK );
	}

	g_pRender->SetShaderResource( 0, TNULL );
	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetDepthEnabled( TTRUE );
}

TBOOL remaster::SysMaterial::Create( BLENDMODE a_eBlendMode )
{
	SetBlendMode( a_eBlendMode );
	ASysMaterial::SetBlendMode( a_eBlendMode );

	return TMaterial::Create();
}

void remaster::SysMaterial::SetBlendMode( BLENDMODE a_eBlendMode )
{
	TASSERT( a_eBlendMode >= BLENDMODE_DEFAULT && a_eBlendMode < BLENDMODE_NUMOF );

	switch ( a_eBlendMode )
	{
		case BLENDMODE_DEFAULT:
			SetOrderTable( g_pSysShader->GetOrderTable( 0 ) );
			break;
		case BLENDMODE_5:
			SetOrderTable( g_pSysShader->GetOrderTable( 3 ) );
			break;
		case BLENDMODE_6:
			SetOrderTable( g_pSysShader->GetOrderTable( 3 ) );
			break;
		case BLENDMODE_7:
			SetOrderTable( g_pSysShader->GetOrderTable( 1 ) );
			break;
		case BLENDMODE_8:
			SetOrderTable( g_pSysShader->GetOrderTable( 2 ) );
			break;
		case BLENDMODE_9:
			SetOrderTable( g_pSysShader->GetOrderTable( 3 ) );
			break;
		default:
			SetOrderTable( g_pSysShader->GetOrderTable( 1 ) );
			break;
	}

	ASysMaterial::SetBlendMode( a_eBlendMode );
}

void remaster::SysMaterial::SetOrderTable( Toshi::TOrderTable* a_pOrderTable )
{
	if ( a_pOrderTable != m_pAssignedOrderTable )
	{
		if ( m_pAssignedOrderTable )
		{
			TOrderTable::DeregisterMaterial( GetRegMaterial() );
		}

		if ( a_pOrderTable )
		{
			a_pOrderTable->RegisterMaterial( this );
		}

		m_pAssignedOrderTable = a_pOrderTable;
	}
}
