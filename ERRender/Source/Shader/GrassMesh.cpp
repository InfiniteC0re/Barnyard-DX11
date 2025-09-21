#include "pch.h"
#include "GrassMesh.h"

#include <Platform/DX8/TRenderContext_DX8.h>
#include <Platform/DX8/TRenderInterface_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

remaster::GrassMesh::GrassMesh()
{
}

remaster::GrassMesh::~GrassMesh()
{
}

TBOOL remaster::GrassMesh::Render()
{
	TRenderD3DInterface* pRenderInterface = TRenderD3DInterface::Interface();

	TRenderPacket* pRenderPacket = GetMaterial()->GetRegMaterial()->AddRenderPacket( this );
	pRenderPacket->SetModelViewMatrix( pRenderInterface->GetCurrentContext()->GetModelViewMatrix() );

	return TTRUE;
}
