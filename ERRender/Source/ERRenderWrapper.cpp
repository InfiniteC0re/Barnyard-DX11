#include "pch.h"
#include "RenderDX11.h"
#include "Shader/GrassShader.h"
#include "Shader/SkinShader.h"
#include "Shader/SkinMesh.h"
#include "Shader/WorldShader.h"
#include "Shader/StaticInstanceShader.h"
#include "Shader/SysShader.h"
#include "Resource/TextureResource.h"
#include "Resource/Viewport.h"
#include "Resource/OrderTable.h"
#include "Resource/ClassPatcher.h"
#include "Resource/VertexBlock.h"
#include "Resource/IndexBlock.h"
#include "UI/UIRenderer.h"
#include "UI/FontRenderer.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Render/TTMDWin.h>

#include <Platform/DX8/TRenderInterface_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static TBOOL __stdcall LoadTRBModelCallback( TModel* a_pModel )
{
	TPROFILER_SCOPE();

	TTMDWin::TRBWinHeader*     pHeader    = a_pModel->CastSymbol<TTMDWin::TRBWinHeader>( "Header" );
	TTMDBase::MaterialsHeader* pMaterials = a_pModel->CastSymbol<TTMDBase::MaterialsHeader>( "Materials" );

	a_pModel->m_iLODCount       = pHeader->m_iNumLODs;
	a_pModel->m_fRenderDistance = pHeader->m_fLODDistance;

	TTMDBase::SHADERTYPE* pShaderTypes = TSTATICCAST( TTMDBase::SHADERTYPE, alloca( pHeader->m_iNumLODs * sizeof( TTMDBase::SHADERTYPE ) ) );

	// Store original information about shaders
	for ( TINT i = 0; i < pHeader->m_iNumLODs; i++ )
	{
		auto pTRBLod = pHeader->GetLOD( i );

		pShaderTypes[ i ] = pTRBLod->m_eShader;

		// Adjust incompatible shader types
		// Those are not supported in the Windows version, but we can use them to adjust material parameters
		switch (pTRBLod->m_eShader)
		{
			case TTMDBase::SHADERTYPE_FOB:
				pTRBLod->m_eShader = TTMDBase::SHADERTYPE_SKIN;
				break;
		}
	}

	// Call original loader callback
	TBOOL bResult = TREINTERPRETCAST( TModel::t_ModelLoaderTRBCallback, 0x006114d0 )( a_pModel );

	// Adjust materials if needed
	if ( bResult )
	{
		for ( TINT i = 0; i < pHeader->m_iNumLODs; i++ )
		{
			TTMDBase::SHADERTYPE eOriginalShader = pShaderTypes[ i ];

			switch ( eOriginalShader )
			{
				case TTMDBase::SHADERTYPE_FOB:
					for ( TINT k = 0; k < a_pModel->m_LODs[ i ].iNumMeshes; k++ )
					{
						remaster::SkinMesh* pMesh = TSTATICCAST( remaster::SkinMesh, a_pModel->m_LODs[ i ].ppMeshes[ k ] );

						pMesh->SetIsFOB( TTRUE );
					}
					break;
			}
		}
	}

	return TFALSE;
}

HOOK( 0x006c6d60, TRenderD3DInterface_CreateObject, TRenderInterface* )
{
	return new remaster::RenderDX11;
}

MEMBER_HOOK( 0x006c72a0, remaster::RenderDX11, TRenderD3DInterface_Create, TBOOL, const char* a_pchWindowTitle )
{
	TModel::SetLoaderTRBCallback( LoadTRBModelCallback );

	return Create( "Barnyard Remastered" );
}

MEMBER_HOOK( 0x006c58e0, remaster::RenderDX11, TRenderD3DInterface_BeginEndScene, void )
{
}

MEMBER_HOOK( 0x006be990, remaster::RenderDX11, TRenderD3DInterface_FlushShaders, void )
{
	FlushOrderTables();

	for ( auto it = TShader::sm_oShaderList.GetRootShader(); it != TNULL; it = it->GetNextShader() )
	{
		it->Flush();
	}
}

MEMBER_HOOK( 0x006d68b0, TD3DAdapter, TD3DAdapter_Mode_Device_SupportsVSConstants, TBOOL )
{
	return TTRUE;
}

void remaster::SetupRenderHooks()
{
	InstallHook<TRenderD3DInterface_Create>();
	InstallHook<TRenderD3DInterface_CreateObject>();
	InstallHook<TRenderD3DInterface_BeginEndScene>();
	InstallHook<TRenderD3DInterface_FlushShaders>();
	InstallHook<TD3DAdapter_Mode_Device_SupportsVSConstants>();

	SetupRenderHooks_GrassShader();
	SetupRenderHooks_SkinShader();
	SetupRenderHooks_WorldShader();
	SetupRenderHooks_TextureResource();
	SetupRenderHooks_Viewport();
	SetupRenderHooks_StaticInstanceShader();
	SetupRenderHooks_SysShader();
	SetupRenderHooks_UIRenderer();
	SetupRenderHooks_FontRenderer();
	SetupRenderHooks_OrderTable();
	SetupRenderHooks_VertexBlock();
	SetupRenderHooks_IndexBlock();
}
