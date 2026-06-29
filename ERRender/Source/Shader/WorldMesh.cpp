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

	/*if ( !TSTATICCAST( AWorldShaderHAL, m_pOwnerShader )->IsAlphaBlendMaterial() ||
	     pCurrentContext->GetAlphaBlend() >= 1.0f )
	{
		pMaterial = TSTATICCAST( AWorldMaterialHAL, m_pMaterial );
	}
	else
	{
		pMaterial = TSTATICCAST( AWorldMaterialHAL, m_pMaterial )->GetAlphaBlendMaterial();
	}*/

	auto  pCtxDX11          = TSTATICCAST( remaster::RenderContextD3D11, pRenderInterface->GetCurrentContext() );
	TBOOL bHasDynamicLights = pCurrentContext->m_oLightIds[ 0 ] >= 0;
	TBOOL bHasStaticLights  = pCtxDX11->GetStaticLightIDs().aIDs[ 0 ] >= 0;
	TBOOL bHasLightData     = bHasDynamicLights || bHasStaticLights;
	auto  pLightData        = bHasLightData ? g_pLightDataPacketAllocator->Allocate() : TNULL;

	if ( pLightData )
	{
		// Both lists are -1-filled when empty, so copy unconditionally; consumers key off slot 0.
		pLightData->oDynamicLights = pCurrentContext->m_oLightIds;
		pLightData->oStaticLights  = pCtxDX11->GetStaticLightIDs();
	}

	auto pRenderPacket = pMaterial->AddRenderPacket( this );
	pRenderPacket->SetModelViewMatrix( pCurrentContext->GetModelViewMatrix() );
	pRenderPacket->SetAlpha( 1.0f );
	pRenderPacket->m_pUnk = pLightData;

	return TTRUE;
}
