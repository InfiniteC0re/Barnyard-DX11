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
