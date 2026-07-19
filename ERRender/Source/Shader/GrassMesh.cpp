#include "pch.h"
#include "GrassMesh.h"
#include "LightManager.h"
#include "RenderContentDX11.h"
#include "Resource/ClassPatcher.h"
#include "LightData.h"

#include <Platform/DX8/TRenderContext_DX8.h>
#include <Platform/DX8/TRenderInterface_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::GrassMesh, 0x0079ab2c );

remaster::GrassMesh::GrassMesh()
{
}

remaster::GrassMesh::~GrassMesh()
{
}

TBOOL remaster::GrassMesh::Render()
{
	TRenderD3DInterface* pRenderInterface = TRenderD3DInterface::Interface();
	auto                 pCurrentContext  = TRenderContextD3D::Upcast( pRenderInterface->GetCurrentContext() );

	auto  pCtxDX11          = TSTATICCAST( remaster::RenderContextD3D11, pRenderInterface->GetCurrentContext() );
	TBOOL bHasDynamicLights = pCurrentContext->m_oLightIds[ 0 ] >= 0;
	TBOOL bHasStaticLights  = pCtxDX11->GetStaticLightIDs()[ 0 ] >= 0;
	TBOOL bHasLightData     = bHasDynamicLights || bHasStaticLights;
	auto  pLightData        = bHasLightData ? g_pLightDataPacketAllocator->Allocate() : TNULL;

	if ( pLightData )
	{
		pLightData->oDynamicLights = pCurrentContext->m_oLightIds;
		const TINT8* pStaticIDs = pCtxDX11->GetStaticLightIDs();
		for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
			pLightData->oStaticLights[ i ] = pStaticIDs[ i ];
	}

	TRenderPacket* pRenderPacket = GetMaterial()->GetRegMaterial()->AddRenderPacket( this );
	pRenderPacket->SetModelViewMatrix( pRenderInterface->GetCurrentContext()->GetModelViewMatrix() );
	pRenderPacket->m_pUnk = pLightData;

	return TTRUE;
}
