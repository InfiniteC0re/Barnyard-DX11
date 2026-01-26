#include "pch.h"
#include "SkinMaterial.h"
#include "SkinShader.h"
#include "Resource/ClassPatcher.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::SkinMaterial, 0x0079a618 );

remaster::SkinMaterial::SkinMaterial()
    : m_pAlphaBlendMaterial( TNULL ), m_pAssignedOrderTable( TNULL ), m_pSomeTexture( TNULL ), m_bIsHDLighting( TTRUE )
{
}

remaster::SkinMaterial::~SkinMaterial()
{
	if ( TNULL != m_pRegMaterial )
	{
		TOrderTable::DeregisterMaterial( m_pRegMaterial );
	}

	if ( TNULL != m_pAlphaBlendMaterial )
	{
		delete m_pAlphaBlendMaterial;
	}
}

void remaster::SkinMaterial::OnDestroy()
{
	if ( m_pAssignedOrderTable )
	{
		TOrderTable::DeregisterMaterial( m_pRegMaterial );
		m_pAssignedOrderTable = TNULL;
	}

	BaseClass::OnDestroy();
}

void remaster::SkinMaterial::PreRender()
{
	TTextureResourceHAL* pTexture    = TSTATICCAST( TTextureResourceHAL, m_pTexture );
	TTextureResourceHAL* pLT0Texture = TNULL;
	TTextureResourceHAL* pLT1Texture = TNULL;

	//auto pD3DTexture = (ID3D11ShaderResourceView*)pTexture->GetD3DTexture();

	if ( m_bIsHDLighting )
	{
		pLT0Texture = TSTATICCAST( TTextureResourceHAL, m_apLightingTextures[ LT_0 ] );
		pLT1Texture = TSTATICCAST( TTextureResourceHAL, m_apLightingTextures[ LT_1 ] );

		g_pRender->PSSetSamplerState( 1, 1 );
		g_pRender->PSSetSamplerState( 2, 1 );
		g_pRender->PSSetSamplerState( 3, 1 );
		g_pRender->PSSetSamplerState( 4, 1 );

// 		pD3DDevice->SetTextureStageState( 1, D3DTSS_MAGFILTER, 2 );
// 		pD3DDevice->SetTextureStageState( 2, D3DTSS_MAGFILTER, 2 );
// 		pD3DDevice->SetTextureStageState( 3, D3DTSS_MAGFILTER, 2 );
// 		pD3DDevice->SetTextureStageState( 4, D3DTSS_MAGFILTER, 2 );

		m_bHasLighting1Tex = pLT0Texture != TNULL;
		m_bHasLighting2Tex = pLT1Texture != TNULL;
	}

	if ( pTexture != TNULL )
	{
		pTexture->Validate();

		if ( pTexture->GetD3DTexture() != TNULL )
		{
			auto pD3DTexture = TREINTERPRETCAST( ID3D11ShaderResourceView*, pTexture->GetD3DTexture() );
			g_pRender->SetShaderResource( 0, pD3DTexture );

			if ( pLT1Texture == TNULL )
			{
				g_pRender->SetShaderResource( 1, TNULL );
				g_pRender->SetShaderResource( 2, TNULL );
			}
			else
			{
				g_pRender->SetShaderResource( 1, TREINTERPRETCAST( ID3D11ShaderResourceView*, pLT1Texture->GetD3DTexture() ) );
				//pD3DDevice->SetTextureStageState( 1, D3DTSS_MIPFILTER, 0 );

				if ( pLT0Texture == TNULL )
				{
					g_pRender->SetShaderResource( 2, TNULL );
				}
				else
				{
					g_pRender->SetShaderResource( 2, TREINTERPRETCAST( ID3D11ShaderResourceView*, pLT0Texture->GetD3DTexture() ) );
					//pD3DDevice->SetTextureStageState( 2, D3DTSS_MIPFILTER, 0 );
				}
			}

			//pD3DDevice->SetTextureStageState( 0, D3DTSS_MIPFILTER, 2 );
			g_pRender->PSSetSamplerState( 0, 3 );
			//pRender->SetTextureAddress( 0, pTexture->GetAddressUState(), TEXCOORD_U );
			//pRender->SetTextureAddress( 0, pTexture->GetAddressVState(), TEXCOORD_V );
		}
	}
	else
	{
		g_pRender->SetShaderResource( 0, TNULL );
	}

// 	if ( m_eBlendMode == 0 || m_eBlendMode == 1 || m_eBlendMode != 3 )
// 	{
// 		pD3DDevice->SetRenderState( D3DRS_SRCBLEND, 5 );
// 		pD3DDevice->SetRenderState( D3DRS_DESTBLEND, 6 );
// 		pD3DDevice->SetRenderState( D3DRS_ZWRITEENABLE, TRUE );
// 	}
// 	else
// 	{
// 		pD3DDevice->SetRenderState( D3DRS_SRCBLEND, 5 );
// 		pD3DDevice->SetRenderState( D3DRS_DESTBLEND, 2 );
// 		pD3DDevice->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
// 	}
// 
// 	if ( m_Flags & FLAGS_NO_CULL )
// 		pD3DDevice->SetRenderState( D3DRS_CULLMODE, 1 );
// 
// 	auto pShader = TDYNAMICCAST( ASkinShaderHAL, GetShader() );
// 	pShader->SetAlphaRef( ( m_Flags & FLAGS_BLENDING ) ? 1 : 128 );
// 
// 	pD3DDevice->SetRenderState( D3DRS_COLORVERTEX, 0 );
// 	pD3DDevice->SetRenderState( D3DRS_CULLMODE, 1 );
// 
// 	auto pRenderContext = TRenderContextD3D::Upcast( pRender->GetCurrentContext() );
// 
// 	if ( pRenderContext->IsFogEnabled() )
// 		pRenderContext->EnableFogHAL();
}

void remaster::SkinMaterial::PostRender()
{
}

TBOOL remaster::SkinMaterial::Create( BLENDMODE a_eBlendMode )
{
	SetBlendMode( a_eBlendMode );
	return BaseClass::Create( a_eBlendMode );
}

void remaster::SkinMaterial::SetBlendMode( BLENDMODE a_eBlendMode )
{
	auto pShader = TSTATICCAST( remaster::SkinShaderDX11, GetShader() );

	switch ( a_eBlendMode )
	{
		case 0:
			SetOrderTable( pShader->GetOrderTable( 0 ) );
			break;
		case 1:
		case 5:
			SetOrderTable( pShader->GetOrderTable( 1 ) );
			break;
		case 2:
		case 6:
			SetOrderTable( pShader->GetOrderTable( 1 ) );
			break;
		case 3:
		case 4:
			SetOrderTable( pShader->GetOrderTable( 1 ) );
			break;
		case 7:
			SetOrderTable( pShader->GetOrderTable( 2 ) );
			break;
	}

	BaseClass::SetBlendMode( a_eBlendMode );
}

void remaster::SkinMaterial::CopyToAlphaBlendMaterial()
{
	if ( TNULL != m_pAlphaBlendMaterial )
	{
		m_pAlphaBlendMaterial->m_iNumTex                    = m_iNumTex;
		m_pAlphaBlendMaterial->m_pTexture                   = m_pTexture;
		m_pAlphaBlendMaterial->m_apLightingTextures[ LT_0 ] = m_apLightingTextures[ LT_0 ];
		m_pAlphaBlendMaterial->m_apLightingTextures[ LT_1 ] = m_apLightingTextures[ LT_1 ];
		m_pAlphaBlendMaterial->m_apLightingTextures[ LT_2 ] = m_apLightingTextures[ LT_2 ];
		m_pAlphaBlendMaterial->m_apLightingTextures[ LT_3 ] = m_apLightingTextures[ LT_3 ];

		m_pAlphaBlendMaterial->SetFlags( TMaterial::FLAGS_BLENDING, TTRUE );
	}
}

void remaster::SkinMaterial::SetOrderTable( Toshi::TOrderTable* a_pOrderTable )
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
