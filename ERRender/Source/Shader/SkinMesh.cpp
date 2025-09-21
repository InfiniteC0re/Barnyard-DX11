#include "pch.h"
#include "SkinMesh.h"
#include "Resource/ClassPatcher.h"

#include <Platform/DX8/TRenderContext_DX8.h>
#include <Platform/DX8/TRenderInterface_DX8.h>
#include <Platform/DX8/TTextureResourceHAL_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::SkinMesh, 0x0079a678 );

remaster::SkinMesh::SkinMesh()
{
}

remaster::SkinMesh::~SkinMesh()
{
}

TBOOL remaster::SkinMesh::Render()
{
	auto pRenderInterface = TRenderD3DInterface::Interface();
	auto pCurrentContext  = TRenderContextD3D::Upcast( pRenderInterface->GetCurrentContext() );

	if ( TSkeletonInstance* pSkeletonInstance = pCurrentContext->GetSkeletonInstance() )
	{
		TMaterial* pMaterial = m_pMaterial;

		/*if ( !TDYNAMICCAST( ASkinShaderHAL, m_pOwnerShader )->IsAlphaBlendMaterial() ||
		     pCurrentContext->GetAlphaBlend() >= 1.0f )
		{
			pMaterial = TDYNAMICCAST( ASkinMaterialHAL, m_pMaterial );
		}
		else
		{
			pMaterial = TDYNAMICCAST( ASkinMaterialHAL, m_pMaterial )->GetAlphaBlendMaterial();
		}*/

		auto pRenderPacket = pMaterial->AddRenderPacket( this );
		pRenderPacket->SetModelViewMatrix( pCurrentContext->GetModelViewMatrix() );
		pRenderPacket->SetSkeletonInstance( pSkeletonInstance );
		pRenderPacket->SetAmbientColour( pCurrentContext->GetAmbientColour().AsVector3() );
		pRenderPacket->SetLightColour( pRenderInterface->GetLightColour().AsBasisVector3( 0 ) );
		pRenderPacket->SetLightDirection( pRenderInterface->GetLightDirection().AsBasisVector3( 0 ) );
		pRenderPacket->SetAlpha( pCurrentContext->GetAlphaBlend() );
		pRenderPacket->SetShadeCoeff( TUINT( pCurrentContext->GetShadeCoeff() * 255.0f ) );

		//ASkinShaderHAL::sm_oWorldViewMatrix = pCurrentContext->GetWorldViewMatrix();
		//ASkinShaderHAL::sm_oViewModelMatrix = pCurrentContext->GetViewModelMatrix();
	}

	return TTRUE;
}
