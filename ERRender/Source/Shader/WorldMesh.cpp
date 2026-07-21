#include "pch.h"
#include "WorldMesh.h"
#include "WorldShader.h"
#include "LightManager.h"
#include "Resource/ClassPatcher.h"
#include "RenderDX11.h"
#include "RenderContentDX11.h"
#include "LightData.h"

#include <Platform/DX8/TRenderInterface_DX8.h>
#include <Platform/DX8/TRenderContext_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::WorldMesh, 0x0079a950 );

remaster::WorldMesh::WorldMesh()
    : m_bHasWorldBounds( TFALSE )
{
}

remaster::WorldMesh::~WorldMesh()
{
}

TBOOL remaster::WorldMesh::Render()
{
	auto pRenderInterface = TRenderD3DInterface::Interface();
	auto pCurrentContext  = TRenderContextD3D::Upcast( pRenderInterface->GetCurrentContext() );

	TMaterial* pMaterial = m_pMaterial;

	if ( remaster::g_pRender->GetCSMManager().IsRenderingShadowPass() && TFALSE )
	{
		pMaterial = TSTATICCAST( remaster::WorldShaderDX11, remaster::WorldShaderDX11::GetSingleton() )->GetShadowMaterial();
	}

	auto  pCtxDX11          = TSTATICCAST( remaster::RenderContextD3D11, pRenderInterface->GetCurrentContext() );
	TBOOL bHasDynamicLights = pCurrentContext->m_oLightIds[ 0 ] >= 0;
	TBOOL bHasStaticLights  = pCtxDX11->GetStaticLightIDs()[ 0 ] >= 0;
	TBOOL bHasLightData     = bHasDynamicLights || bHasStaticLights;
	auto  pLightData        = bHasLightData ? g_pLightDataPacketAllocator->Allocate() : TNULL;

	if ( pLightData )
	{
		// Both lists are -1-filled when empty, so copy unconditionally; consumers key off slot 0.
		pLightData->oDynamicLights = pCurrentContext->m_oLightIds;
		const TINT8* pStaticIDs = pCtxDX11->GetStaticLightIDs();
		for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
			pLightData->oStaticLights[ i ] = pStaticIDs[ i ];
	}

	auto pRenderPacket = pMaterial->AddRenderPacket( this );
	pRenderPacket->SetModelViewMatrix( pCurrentContext->GetModelViewMatrix() );
	pRenderPacket->SetAlpha( 1.0f );

	// Snapshot light-colour row 0 like SkinMesh. ATreeManager2 passes the per-instance FOB tint
	// selector (sunExposure, tintType, -1) through it; WorldShader::Render consumes it for "fob"
	// materials (z = -1 marks a valid selector)
	pRenderPacket->SetLightColour( pRenderInterface->GetLightColour().AsBasisVector3( 0 ) );
	pRenderPacket->m_pUnk = pLightData;

	return TTRUE;
}
