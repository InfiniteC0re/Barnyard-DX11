#include "pch.h"
#include "OrderTable.h"
#include "TextureResource.h"
#include "RenderDX11.h"
#include "RenderParams.h"
#include "CSM/CSMManager.h"
#include "Shader/WorldMesh.h"
#include "Shader/WorldShader.h"
#include "Shader/SkinShader.h"
#include "Shader/SkinMaterial.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Render/TOrderTable.h>
#include <Render/TShader.h>

#include <vector>
#include <algorithm>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

struct DepthSortMatEntry
{
	TFLOAT        fMinDepth;
	TRegMaterial* pRegMat;
	TINT8         iTier; // 0 = plain opaque, 1 = alpha tested, 2 = blended
};

struct DepthSortPacketEntry
{
	TFLOAT         fDepth;
	TRenderPacket* pPacket;
};

// Reused across flushes to avoid per-frame allocation churn
static std::vector<DepthSortMatEntry>    s_vecSortMats;
static std::vector<DepthSortPacketEntry> s_vecSortPackets;

// Front-to-back depth sort for the scene passes
static void DepthSortRegMatList( TRegMaterial*& a_rpLastRegMat )
{
	s_vecSortMats.clear();

	for ( TRegMaterial* pRegMat = a_rpLastRegMat; pRegMat != TNULL; pRegMat = pRegMat->GetNextRegMat() )
	{
		TMaterial* pMaterial = pRegMat->GetMaterial();

		DepthSortMatEntry oEntry;
		oEntry.pRegMat   = pRegMat;
		oEntry.fMinDepth = 1e30f;

		const TBOOL bBlend          = pMaterial && ( pMaterial->IsBlending() || ( pMaterial->GetFlags() & TMaterial::FLAGS_BLENDING ) );
		const TBOOL bWorldShaderMat = pMaterial && pMaterial->GetShader() == remaster::WorldShaderDX11::GetSingleton();

		Toshi::TTexture* pTexture = TNULL;
		if ( bWorldShaderMat )
			pTexture = TSTATICCAST( AWorldMaterial, pMaterial )->GetTexture( 0 );
		else if ( pMaterial && pMaterial->GetShader() == remaster::SkinShaderDX11::GetSingleton() )
			pTexture = TSTATICCAST( ASkinMaterial, pMaterial )->GetTexture();

		oEntry.iTier = bBlend ? 2 : ( remaster::TextureResource_HasTransparency( pTexture ) ? 1 : 0 );

		s_vecSortPackets.clear();
		for ( TRenderPacket* pPacket = pRegMat->m_pLastRenderPacket; pPacket != TNULL; pPacket = pPacket->GetNextPacket() )
		{
			// View-space depth of the packet's origin (+Z into the screen)
			TFLOAT fDepth = pPacket->GetModelViewMatrix().GetTranslation3().z;

			if ( bWorldShaderMat )
			{
				auto pWorldMesh = TSTATICCAST( remaster::WorldMesh, pPacket->GetMesh() );
				if ( pWorldMesh && pWorldMesh->HasWorldBounds() )
				{
					TVector3 vecViewCentre;
					TMatrix44::TransformVector( vecViewCentre, pPacket->GetModelViewMatrix(), pWorldMesh->GetWorldBoundsCentre() );
					fDepth = vecViewCentre.z;
				}
			}
			if ( pPacket->GetAlpha() < 1.0f )
				oEntry.iTier = 2;
			if ( fDepth < oEntry.fMinDepth )
				oEntry.fMinDepth = fDepth;
			s_vecSortPackets.push_back( { fDepth, pPacket } );
		}

		if ( oEntry.iTier != 2 && s_vecSortPackets.size() > 2 )
		{
			std::sort( s_vecSortPackets.begin(), s_vecSortPackets.end(), []( const DepthSortPacketEntry& a_rcA, const DepthSortPacketEntry& a_rcB ) {
				return a_rcA.fDepth < a_rcB.fDepth;
			} );

			// Rebuild the chain nearest-first (head is rendered first)
			TRenderPacket* pHead = TNULL;
			for ( size_t i = s_vecSortPackets.size(); i-- > 0; )
			{
				s_vecSortPackets[ i ].pPacket->SetNextPacket( pHead );
				pHead = s_vecSortPackets[ i ].pPacket;
			}
			pRegMat->m_pLastRenderPacket = pHead;
		}

		s_vecSortMats.push_back( oEntry );
	}

	if ( s_vecSortMats.size() < 2 )
		return;

	std::stable_sort( s_vecSortMats.begin(), s_vecSortMats.end(), []( const DepthSortMatEntry& a_rcA, const DepthSortMatEntry& a_rcB ) {
		if ( a_rcA.iTier != a_rcB.iTier )
			return a_rcA.iTier < a_rcB.iTier;
		if ( a_rcA.iTier == 2 )
			return false;
		return a_rcA.fMinDepth < a_rcB.fMinDepth;
	} );

	TRegMaterial* pHead = TNULL;
	for ( size_t i = s_vecSortMats.size(); i-- > 0; )
	{
		s_vecSortMats[ i ].pRegMat->SetNextRegMat( pHead );
		pHead = s_vecSortMats[ i ].pRegMat;
	}
	a_rpLastRegMat = pHead;
}

HOOK( 0x006d5a60, TOrderTable_CreateStaticData, void, TUINT a_uiMaxMaterials, TUINT a_uiMaxRenderPackets )
{
	Toshi::TOrderTable::CreateStaticData( a_uiMaxMaterials, a_uiMaxRenderPackets * 2 );
}

MEMBER_HOOK( 0x006d58e0, TOrderTable, TOrderTable_Create, void, Toshi::TShader* a_pShader, TINT a_iPriority )
{
	m_pLastRegMat = TNULL;
	m_pShader     = a_pShader;
	remaster::g_pRender->GetOrderTables().Insert( this, a_iPriority );
}

void remaster::DepthSortOrderTable( TOrderTable* a_pOrderTable )
{
	if ( g_bDepthSortOrderTables && g_bInMainScenePass && a_pOrderTable->m_pLastRegMat != TNULL )
		DepthSortRegMatList( a_pOrderTable->m_pLastRegMat );
}

MEMBER_HOOK( 0x006d5be0, TOrderTable, TOrderTable_RegisterMaterial, void, Toshi::TMaterial* a_pMaterial )
{
	RegisterMaterial( a_pMaterial );
}

MEMBER_HOOK( 0x006d5c60, TOrderTable, TOrderTable_DeregisterMaterial, void, Toshi::TRegMaterial* a_pRegMat )
{
	DeregisterMaterial( a_pRegMat );
}

MEMBER_HOOK( 0x006d5970, TOrderTable, TOrderTable_Flush, void )
{
	TPROFILER_SCOPE();

	if ( s_uiMaxNumRenderPackets < s_uiNumRenderPackets )
	{
		s_uiMaxNumRenderPackets = s_uiNumRenderPackets;
	}

	if ( m_pLastRegMat != TNULL )
	{
		remaster::DepthSortOrderTable( this );

		m_pShader->StartFlush();

		for ( auto it = m_pLastRegMat; it != TNULL; it = it->GetNextRegMat() )
		{
			it->Render();
		}

		m_pShader->EndFlush();
	}

	s_uiNumRenderPackets = 0;
	m_pLastRegMat        = TNULL;
}

MEMBER_HOOK( 0x006d5910, TOrderTable, TOrderTable_Render, void )
{
	TPROFILER_SCOPE();

	if ( m_pLastRegMat != TNULL )
	{
		for ( auto it = m_pLastRegMat; it != TNULL; it = it->GetNextRegMat() )
		{
			it->Render();
		}
	}

	s_uiNumRenderPackets = 0;
	m_pLastRegMat        = TNULL;
}

MEMBER_HOOK( 0x006d5d60, TOrderTable, TOrderTable_Destructor, void )
{
	( (TOrderTable*)( this ) )->~TOrderTable();
}

void remaster::SetupRenderHooks_OrderTable()
{
	InstallHook<TOrderTable_CreateStaticData>();
	InstallHook<TOrderTable_Create>();
	InstallHook<TOrderTable_RegisterMaterial>();
	InstallHook<TOrderTable_DeregisterMaterial>();
	InstallHook<TOrderTable_Flush>();
	InstallHook<TOrderTable_Render>();
	InstallHook<TOrderTable_Destructor>();
}
