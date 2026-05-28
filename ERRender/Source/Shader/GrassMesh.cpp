#include "pch.h"
#include "GrassMesh.h"
#include "DynamicGlowLights.h"
#include "Resource/ClassPatcher.h"

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

	TRenderPacket* pRenderPacket = GetMaterial()->GetRegMaterial()->AddRenderPacket( this );
	pRenderPacket->SetModelViewMatrix( pRenderInterface->GetCurrentContext()->GetModelViewMatrix() );
	pRenderPacket->m_ui8Unk1 = pCurrentContext->m_oLightIds[ 0 ];
	pRenderPacket->m_pUnk    = PackRenderPacketLights( pCurrentContext->m_oLightIds );

	return TTRUE;
}
