#include "pch.h"
#include "WorldMaterial.h"
#include "WorldShader.h"
#include "Resource/ClassPatcher.h"

#include <Toshi/TScheduler.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::WorldMaterial, 0x0079a9b0 );

remaster::WorldMaterial::WorldMaterial()
{
	m_pAlphaBlendMaterial = TNULL;
	m_pAssignedOrderTable = TNULL;

	for ( TUINT i = 0; i < MAX_TEXTURES; i++ )
	{
		m_aHasUVOffsets[ i ] = TFALSE;
		m_aUVOffsetsX[ i ]   = 0.0f;
		m_aUVOffsetsY[ i ]   = 0.0f;
	}
}

remaster::WorldMaterial::~WorldMaterial()
{
	if ( m_pRegMaterial )
	{
		TOrderTable::DeregisterMaterial( m_pRegMaterial );
	}

	if ( m_pAlphaBlendMaterial )
	{
		delete m_pAlphaBlendMaterial;
	}
}

void remaster::WorldMaterial::OnDestroy()
{
	if ( m_pAssignedOrderTable != TNULL )
	{
		TOrderTable::DeregisterMaterial( m_pRegMaterial );
		m_pAssignedOrderTable = TNULL;
	}

	BaseClass::OnDestroy();
}

void remaster::WorldMaterial::PreRender()
{
	// Animate UV
	TFLOAT fDeltaTime = Toshi::g_oSystemManager.GetScheduler()->GetCurrentDeltaTime();
	m_fUVAnimX += fDeltaTime * m_fUVAnimSpeedX;
	m_fUVAnimY += fDeltaTime * m_fUVAnimSpeedY;

	// Make sure value of m_fUVAnimX is in [-1; 1] range so we won't overflow
	if ( m_fUVAnimX <= 1.0f )
	{
		if ( m_fUVAnimX < -1.0f && !isnan( m_fUVAnimX ) )
		{
			m_fUVAnimX += 1.0f;
		}
	}
	else
	{
		m_fUVAnimX -= 1.0f;
	}

	// Make sure value of m_fUVAnimY is in [-1; 1] range so we won't overflow
	if ( m_fUVAnimY <= 1.0f )
	{
		if ( m_fUVAnimY < -1.0f && !isnan( m_fUVAnimY ) )
		{
			m_fUVAnimY += 1.0f;
		}
	}
	else
	{
		m_fUVAnimY -= 1.0f;
	}

	// Save current UV offset
	m_aHasUVOffsets[ 0 ] = TTRUE;
	m_aUVOffsetsX[ 0 ]   = m_fUVAnimX;
	m_aUVOffsetsY[ 0 ]   = m_fUVAnimY;
}

void remaster::WorldMaterial::PostRender()
{
	
}

TBOOL remaster::WorldMaterial::Create( BLENDMODE a_eBlendMode )
{
	SetBlendMode( a_eBlendMode );
	return AWorldMaterial::Create( a_eBlendMode );
}

void remaster::WorldMaterial::SetBlendMode( BLENDMODE a_eBlendMode )
{
	auto pShader = TDYNAMICCAST( WorldShaderDX11, m_pShader );

	switch ( a_eBlendMode )
	{
		case 0:
			SetOrderTable( pShader->GetOrderTable( 0 ) );
			break;
		case 1:
			SetOrderTable( pShader->GetOrderTable( 2 ) );
			break;
		case 2:
			SetOrderTable( pShader->GetOrderTable( 2 ) );
			break;
		case 3:
			SetOrderTable( pShader->GetOrderTable( 3 ) );
			break;
		case 5:
			SetOrderTable( pShader->GetOrderTable( 6 ) );
			break;
		case 6:
			SetOrderTable( pShader->GetOrderTable( 4 ) );
			break;
		case 7:
			SetOrderTable( pShader->GetOrderTable( 7 ) );
			break;
		default:
			TASSERT( !"Invalid blend mode" );
			break;
	}

	BaseClass::SetBlendMode( a_eBlendMode );
}

void remaster::WorldMaterial::CopyToAlphaBlendMaterial()
{
	if ( m_pAlphaBlendMaterial )
	{
		m_pAlphaBlendMaterial->m_iNumTex        = m_iNumTex;
		m_pAlphaBlendMaterial->m_aTextures[ 0 ] = m_aTextures[ 0 ];
		m_pAlphaBlendMaterial->m_aTextures[ 1 ] = m_aTextures[ 1 ];
		m_pAlphaBlendMaterial->m_aTextures[ 2 ] = m_aTextures[ 2 ];
		m_pAlphaBlendMaterial->m_aTextures[ 3 ] = m_aTextures[ 3 ];

		m_pAlphaBlendMaterial->SetFlags( FLAGS_BLENDING, TTRUE );
	}
}

void remaster::WorldMaterial::SetOrderTable( Toshi::TOrderTable* a_pOrderTable, TINT a_iUnused )
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
