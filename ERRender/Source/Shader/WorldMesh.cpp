#include "pch.h"
#include "WorldMesh.h"
#include "Resource/ClassPatcher.h"

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

	/*if ( !TSTATICCAST( AWorldShaderHAL, m_pOwnerShader )->IsAlphaBlendMaterial() ||
	     pCurrentContext->GetAlphaBlend() >= 1.0f )
	{
		pMaterial = TSTATICCAST( AWorldMaterialHAL, m_pMaterial );
	}
	else
	{
		pMaterial = TSTATICCAST( AWorldMaterialHAL, m_pMaterial )->GetAlphaBlendMaterial();
	}*/

	auto pRenderPacket = pMaterial->AddRenderPacket( this );
	pRenderPacket->SetModelViewMatrix( pCurrentContext->GetModelViewMatrix() );
	pRenderPacket->SetAlpha( 1.0f );

	return TTRUE;
}
