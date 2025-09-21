#include "pch.h"
#include "SysMesh.h"
#include "Resource/ClassPatcher.h"

#include <Render/TRenderPacket.h>
#include <Render/TRenderInterface.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::SysMesh, 0x0079a468 );

remaster::SysMesh::SysMesh()
    : m_iZBias( 0 )
{
}

remaster::SysMesh::~SysMesh()
{
}

TBOOL remaster::SysMesh::Render()
{
	if ( ms_bStopRendering == FALSE )
	{
		auto pRenderPacket = GetMaterial()->AddRenderPacket( this );

		pRenderPacket->SetModelViewMatrix(
		    TRenderInterface::GetSingleton()->GetCurrentContext()->GetModelViewMatrix()
		);
	}

	return TTRUE;
}

void remaster::SysMesh::SetZBias( TINT a_iZBias )
{
	m_iZBias = a_iZBias;
}
