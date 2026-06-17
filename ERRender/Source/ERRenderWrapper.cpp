#include "pch.h"
#include "RenderDX11.h"
#include "Shader/GrassShader.h"
#include "Shader/SkinShader.h"
#include "Shader/SkinMesh.h"
#include "Shader/WorldShader.h"
#include "Shader/StaticInstanceShader.h"
#include "Shader/SysShader.h"
#include "Ref/AWorld.h"
#include "Ref/AWorldVIS.h"
#include "CSM/CSMShadowBatch.h"
#include "Resource/TextureResource.h"
#include "Resource/Viewport.h"
#include "Resource/OrderTable.h"
#include "Resource/ClassPatcher.h"
#include "Resource/VertexBlock.h"
#include "Resource/IndexBlock.h"
#include "UI/UIRenderer.h"
#include "UI/FontRenderer.h"

#include "Generated/SkyMaskShaderCombos.h"
#include "Generated/SunShaftsShaderCombos.h"
#include "Generated/CopyTextureShaderCombos.h"
#include "Generated/ResolveDepthShaderCombos.h"
#include "Generated/DualKawaseDownShaderCombos.h"
#include "Generated/DualKawaseUpShaderCombos.h"
#include "Generated/GlowBloomCompositeShaderCombos.h"
#include "Generated/HBAOPlusShaderCombos.h"
#include "Generated/XeGTAOShaderCombos.h"
#include "Generated/HBAOBlurShaderCombos.h"
#include "Generated/HBAOCompositeShaderCombos.h"
#include "Generated/VolumetricFogShaderCombos.h"
#include "Generated/VolumetricFogCompositeShaderCombos.h"
#include "RenderContentDX11.h"
#include "DynamicGlowLights.h"

#include <Toshi/TTask.h>
#include <Render/TTMDWin.h>

#include <AHooks.h>
#include <HookHelpers.h>
#include <BYardSDK/ACamera.h>
#include <BYardSDK/ARenderer.h>
#include <BYardSDK/AGlowViewport.h>

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
			case TTMDBase::SHADERTYPE_STATICINSTANCE:
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
				case TTMDBase::SHADERTYPE_STATICINSTANCE:
					for ( TINT k = 0; k < a_pModel->m_LODs[ i ].iNumMeshes; k++ )
					{
						remaster::SkinMesh* pMesh = TSTATICCAST( remaster::SkinMesh, a_pModel->m_LODs[ i ].ppMeshes[ k ] );

						pMesh->SetIsFOB( TFALSE );
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

HOOK( 0x005e83e0, RenderCellMeshWin, void, CellMeshSphere* a_pMeshSphere, RenderData* a_pRenderData )
{
	TVALIDPTR( a_pMeshSphere );

	const TBOOL bNormalPass = !remaster::g_pRender->GetCSMManager().IsRenderingShadowPass();

	ATerrainInterface* pTerrainInterface = ATerrainInterface::GetSingleton();
	TMesh*             pMesh             = a_pMeshSphere->m_pCellMesh->pMesh;

	// Get light mag
	TFLOAT fLightMag = 0.6f;
	for ( TUINT i = 0; i < 12; i++ )
	{
		if ( pTerrainInterface->m_apLightMagMaterials[ i ] == pMesh->GetMaterial() )
		{
			fLightMag = pTerrainInterface->m_afLightMags[ i ];

			if ( fLightMag <= 0.0f )
				return;

			break;
		}
	}

	auto pContext = g_pRender->GetCurrentContext();

	if ( bNormalPass )
	{
		TSphere oBounding(
			a_pMeshSphere->m_BoundingSphere.AsVector4().x,
			-a_pMeshSphere->m_BoundingSphere.AsVector4().z,
			a_pMeshSphere->m_BoundingSphere.AsVector4().y,
			a_pMeshSphere->m_BoundingSphere.GetRadius()
		);

		TLightIDList oLightIdList;
		AGlowViewport::GetSingleton()->GetInfluencingLightIDs( oBounding, oLightIdList );

		pContext->ClearLightIDs();
		if ( oLightIdList[ 0 ] >= 0 ) pContext->AddLight( oLightIdList[ 0 ] );
		if ( oLightIdList[ 1 ] >= 0 ) pContext->AddLight( oLightIdList[ 1 ] );
		if ( oLightIdList[ 2 ] >= 0 ) pContext->AddLight( oLightIdList[ 2 ] );
		if ( oLightIdList[ 3 ] >= 0 ) pContext->AddLight( oLightIdList[ 3 ] );
	}

	TVector4 vecColour{ 0.3f, 0.3f, 0.1952941f, 1.0f };
	vecColour *= fLightMag;

	TSTATICCAST( remaster::WorldShaderDX11, remaster::WorldShaderDX11::GetSingleton() )->SetColours( vecColour, vecColour );
	pMesh->Render();

	if ( bNormalPass ) pContext->ClearLightIDs();
}

HOOK( 0x005e7d10, RenderCellMeshDefault, void, CellMeshSphere* a_pMeshSphere, RenderData* a_pRenderData )
{
	TVALIDPTR( a_pMeshSphere );

	const TBOOL bNormalPass = !remaster::g_pRender->GetCSMManager().IsRenderingShadowPass();

	auto pContext = g_pRender->GetCurrentContext();

	if ( bNormalPass )
	{
		TSphere oBounding(
			a_pMeshSphere->m_BoundingSphere.AsVector4().x,
			-a_pMeshSphere->m_BoundingSphere.AsVector4().z,
			a_pMeshSphere->m_BoundingSphere.AsVector4().y,
			a_pMeshSphere->m_BoundingSphere.GetRadius()
		);

		TLightIDList oLightIdList;
		AGlowViewport::GetSingleton()->GetInfluencingLightIDs( oBounding, oLightIdList );

		pContext->ClearLightIDs();
		if ( oLightIdList[ 0 ] >= 0 ) pContext->AddLight( oLightIdList[ 0 ] );
		if ( oLightIdList[ 1 ] >= 0 ) pContext->AddLight( oLightIdList[ 1 ] );
		if ( oLightIdList[ 2 ] >= 0 ) pContext->AddLight( oLightIdList[ 2 ] );
		if ( oLightIdList[ 3 ] >= 0 ) pContext->AddLight( oLightIdList[ 3 ] );
	}

	a_pMeshSphere->m_pCellMesh->pMesh->Render();

	if ( bNormalPass ) pContext->ClearLightIDs();
}

// Build merged per-material shadow buffers for each streamed world section once
// its meshes are loaded. The original walks the WorldDatabase and fills per-mesh
// GPU pools; afterwards every CellMesh has a valid pMesh, so we can group them.
HOOK( 0x00613a40, AModelLoader_LoadWorldMeshTRB_Shadow, void, TModel* a_pModel, TINT a_iLODIndex, TModelLOD* a_pLOD, TTMDWin::TRBLODHeader* a_pLODHeader )
{
	CallOriginal( a_pModel, a_iLODIndex, a_pLOD, a_pLODHeader );

	//remaster::CSMShadowBatch::GetSingleton().BuildSection( a_pModel, a_pLOD );
}

static ID3D11Texture2D*          s_pSkyMaskTexture            = TNULL;
static ID3D11RenderTargetView*   s_pSkyMaskRenderTargetView   = TNULL;
static ID3D11ShaderResourceView* s_pSkyMaskShaderResourceView = TNULL;

static ID3D11Texture2D*          s_pSunshaftsTexture            = TNULL;
static ID3D11RenderTargetView*   s_pSunshaftsRenderTargetView   = TNULL;
static ID3D11ShaderResourceView* s_pSunshaftsShaderResourceView = TNULL;

static ID3D11Texture2D*          s_pResolvedColorTexture = TNULL;
static ID3D11ShaderResourceView* s_pResolvedColorSRV     = TNULL;
static ID3D11Texture2D*          s_pResolvedGlowTexture  = TNULL;
static ID3D11ShaderResourceView* s_pResolvedGlowSRV      = TNULL;

static ID3D11Texture2D*          s_pResolvedDepthTexture = TNULL;
static ID3D11RenderTargetView*   s_pResolvedDepthRTV     = TNULL;
static ID3D11ShaderResourceView* s_pResolvedDepthSRV     = TNULL;

static ID3D11Texture2D*          s_pHBAOTexture    = TNULL;
static ID3D11RenderTargetView*   s_pHBAORTV        = TNULL;
static ID3D11ShaderResourceView* s_pHBAOSRV        = TNULL;
static ID3D11Texture2D*          s_pHBAOBlurTexture = TNULL;
static ID3D11RenderTargetView*   s_pHBAOBlurRTV     = TNULL;
static ID3D11ShaderResourceView* s_pHBAOBlurSRV     = TNULL;

// AO is computed and blurred at half-width x half-height (quarter the pixels),
// then upsampled with a linear sampler during the composite pass.
static TUINT                     s_uiHBAOWidth     = 0;
static TUINT                     s_uiHBAOHeight    = 0;

static ID3D11Texture2D*          s_pVolumetricFogTexture = TNULL;
static ID3D11RenderTargetView*   s_pVolumetricFogRTV     = TNULL;
static ID3D11ShaderResourceView* s_pVolumetricFogSRV     = TNULL;
static ID3D11Texture2D*          s_pVolumetricFogTemporalTexture = TNULL;
static ID3D11RenderTargetView*   s_pVolumetricFogTemporalRTV     = TNULL;
static ID3D11ShaderResourceView* s_pVolumetricFogTemporalSRV     = TNULL;
static ID3D11Texture2D*          s_pVolumetricFogHistoryTexture  = TNULL;
static ID3D11RenderTargetView*   s_pVolumetricFogHistoryRTV      = TNULL;
static ID3D11ShaderResourceView* s_pVolumetricFogHistorySRV      = TNULL;
static TBOOL                     s_bVolumetricFogHistoryValid    = TFALSE;
static TUINT                     s_uiVolumetricFogFrameIndex     = 0;

static constexpr TINT KAWASE_MAX_LEVELS = 5;

static ID3D11Texture2D*          s_pKawaseTextures[ KAWASE_MAX_LEVELS ] = {};
static ID3D11RenderTargetView*   s_pKawaseRTVs[ KAWASE_MAX_LEVELS ]     = {};
static ID3D11ShaderResourceView* s_pKawaseSRVs[ KAWASE_MAX_LEVELS ]     = {};
static TUINT                     s_uiKawaseWidths[ KAWASE_MAX_LEVELS ]   = {};
static TUINT                     s_uiKawaseHeights[ KAWASE_MAX_LEVELS ]  = {};

struct KawaseCBuffer
{
	TFLOAT texelSizeX;
	TFLOAT texelSizeY;
	TFLOAT offset;
	TFLOAT PADDING;
};

static ID3D11Buffer*       s_pKawaseCBuffer       = TNULL;
static ID3D11SamplerState* s_pPointClampSampler   = TNULL;
static ID3D11SamplerState* s_pLinearClampSampler  = TNULL;

struct HBAOCBuffer
{
	TFLOAT projection[ 4 ];
	TFLOAT depthParams[ 4 ];
	TFLOAT params[ 4 ];
	TFLOAT bufferSize[ 4 ];
};

struct XeGTAOCBuffer
{
	TFLOAT projection[ 4 ];
	TFLOAT depthParams[ 4 ];
	TFLOAT params[ 4 ];
	TFLOAT bufferSize[ 4 ];
	TFLOAT xeParams[ 4 ];
};

struct HBAOBlurCBuffer
{
	TFLOAT blurParams[ 4 ];
	TFLOAT depthParams[ 4 ];
};

static ID3D11Buffer* s_pHBAOConstantBuffer          = TNULL;
static ID3D11Buffer* s_pXeGTAOConstantBuffer        = TNULL;
static ID3D11Buffer* s_pHBAOBlurConstantBuffer      = TNULL;

struct SunShaftsCBuffer
{
	TFLOAT vSunPos[ 2 ];
	TFLOAT fSunAlpha;
	TFLOAT fRaysLength;
	TFLOAT vRaysTint[ 3 ];
	TFLOAT PADDING;
};

static ID3D11Buffer*        s_pSunShaftsConstantBuffer = TNULL;
static ID3D11SamplerState*  s_pSkyMaskSampler          = TNULL;

struct VolumetricFogCBuffer
{
	TFLOAT matLightVP[ 3 ][ 16 ];
	TFLOAT cascadeSplits[ 4 ];
	TFLOAT shadowBias[ 4 ];
	TMatrix44 matViewWorld;
	TFLOAT projection[ 4 ];
	TFLOAT depthParams[ 4 ];
	TFLOAT lightDirVS[ 4 ];
	TFLOAT fogColor[ 4 ];
	TFLOAT fogParams[ 4 ];
	TFLOAT frameParams[ 4 ];
};

struct VolumetricFogCompositeCBuffer
{
	TFLOAT depthParams[ 4 ];
	TFLOAT compositeParams[ 4 ];
};

static ID3D11Buffer* s_pVolumetricFogConstantBuffer          = TNULL;
static ID3D11Buffer* s_pVolumetricFogCompositeConstantBuffer = TNULL;

namespace remaster
{
TBOOL  g_bSunShaftsEnabled       = TTRUE;
TFLOAT g_flSunShaftsAlpha        = 0.04f;
TFLOAT g_flSunShaftsRaysLength   = 0.3f;
TFLOAT g_flSunShaftsTint[ 3 ]    = { 1.0f, 0.91f, 0.8f };
TINT   g_iSunShaftsKawaseLevels  = 1;
TFLOAT g_flSunShaftsKawaseOffset = 1.0f;

TBOOL  g_bGlowBloomEnabled       = TTRUE;
TINT   g_iGlowBloomKawaseLevels  = 2;
TFLOAT g_flGlowBloomKawaseOffset = 1.7f;
TFLOAT g_flGlowBloomIntensity    = 0.715f;

TBOOL  g_bHBAOEnabled       = TTRUE;
TBOOL  g_bHBAODebug         = TFALSE;
TINT   g_iAOAlgorithm       = 0;
TFLOAT g_flHBAORadius       = 0.7f;
TFLOAT g_flHBAOSceneScale   = 1.0f;
TFLOAT g_flHBAOBias         = 0.10f;
TFLOAT g_flHBAOIntensity    = 1.0f;
TFLOAT g_flHBAOPower        = 1.0f;
TFLOAT g_flHBAOBlurSharpness = 4.0f;
TFLOAT g_flXeGTAORadiusMultiplier = 1.457f;
TFLOAT g_flXeGTAOFalloffRange = 0.9f;
TFLOAT g_flXeGTAOSampleDistributionPower = 2.8f;
TFLOAT g_flXeGTAOThinOccluderCompensation = 0.5f;

TBOOL  g_bVolumetricFogEnabled       = TTRUE;
TINT   g_iVolumetricFogCompositeMode = 0;
TFLOAT g_flVolumetricFogDensity      = 0.019f;
TFLOAT g_flVolumetricFogG            = 0.0f;
TFLOAT g_flVolumetricFogMaxDist      = 44.0f;
TFLOAT g_flVolumetricFogIntensity    = 0.16f;
TFLOAT g_flVolumetricFogColor[ 3 ]   = { 0.937f, 0.8f, 0.5254f };

} // namespace remaster

void remaster::RenderDX11::CreateRenderTargets()
{
	auto pSwapChainDesc = GetSwapChainDesc();

	// Sky mask
	{
		D3D11_TEXTURE2D_DESC skyMaskDesc = {};
		skyMaskDesc.ArraySize            = 1;
		skyMaskDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		skyMaskDesc.CPUAccessFlags       = 0;
		skyMaskDesc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
		skyMaskDesc.Height               = pSwapChainDesc->BufferDesc.Height >> 1;
		skyMaskDesc.Width                = pSwapChainDesc->BufferDesc.Width >> 1;
		skyMaskDesc.MipLevels            = 1;
		skyMaskDesc.MiscFlags            = 0;
		// Sky-mask and sunshafts are fullscreen post-process buffers produced by a
		// fullscreen triangle -- no geometric aliasing to resolve.  Forcing them to
		// 1-sample cuts both memory footprint and RT write bandwidth by 4x vs
		// matching the swap-chain MSAA setting.
		skyMaskDesc.SampleDesc.Count     = 1;
		skyMaskDesc.SampleDesc.Quality   = 0;
		skyMaskDesc.Usage                = D3D11_USAGE_DEFAULT;

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &skyMaskDesc, TNULL, &s_pSkyMaskTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pSkyMaskTexture, TNULL, &s_pSkyMaskRenderTargetView ) );

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &skyMaskDesc, TNULL, &s_pSunshaftsTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pSunshaftsTexture, TNULL, &s_pSunshaftsRenderTargetView ) );

		D3D11_SHADER_RESOURCE_VIEW_DESC skyMaskSRVDesc = {};
		skyMaskSRVDesc.Format                          = skyMaskDesc.Format;
		skyMaskSRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
		skyMaskSRVDesc.Texture2D.MipLevels             = 1;
		skyMaskSRVDesc.Texture2D.MostDetailedMip       = 0;

		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pSkyMaskTexture, &skyMaskSRVDesc, &s_pSkyMaskShaderResourceView ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pSunshaftsTexture, &skyMaskSRVDesc, &s_pSunshaftsShaderResourceView ) );

		D3D11_BUFFER_DESC sunShaftsCBDesc = {};
		sunShaftsCBDesc.ByteWidth      = sizeof( SunShaftsCBuffer );
		sunShaftsCBDesc.Usage          = D3D11_USAGE_DYNAMIC;
		sunShaftsCBDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
		sunShaftsCBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &sunShaftsCBDesc, TNULL, &s_pSunShaftsConstantBuffer ) );
	}

	// Resolved color/glow: non-MSAA destinations for ResolveSubresource
	{
		D3D11_TEXTURE2D_DESC desc  = {};
		desc.Width                 = pSwapChainDesc->BufferDesc.Width;
		desc.Height                = pSwapChainDesc->BufferDesc.Height;
		desc.MipLevels             = 1;
		desc.ArraySize             = 1;
		desc.Format                = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count      = 1;
		desc.SampleDesc.Quality    = 0;
		desc.Usage                 = D3D11_USAGE_DEFAULT;
		desc.BindFlags             = D3D11_BIND_SHADER_RESOURCE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pResolvedColorTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pResolvedColorTexture, TNULL, &s_pResolvedColorSRV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pResolvedGlowTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pResolvedGlowTexture, TNULL, &s_pResolvedGlowSRV ) );
	}

	// Resolved depth: R32_FLOAT render target written by the ResolveDepth shader
	{
		D3D11_TEXTURE2D_DESC desc  = {};
		desc.Width                 = pSwapChainDesc->BufferDesc.Width;
		desc.Height                = pSwapChainDesc->BufferDesc.Height;
		desc.MipLevels             = 1;
		desc.ArraySize             = 1;
		desc.Format                = DXGI_FORMAT_R32_FLOAT;
		desc.SampleDesc.Count      = 1;
		desc.SampleDesc.Quality    = 0;
		desc.Usage                 = D3D11_USAGE_DEFAULT;
		desc.BindFlags             = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pResolvedDepthTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pResolvedDepthTexture, TNULL, &s_pResolvedDepthRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pResolvedDepthTexture, TNULL, &s_pResolvedDepthSRV ) );
	}

	// HBAO+ style AO buffers (computed at half resolution -- quarter the pixels)
	{
		s_uiHBAOWidth  = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Width  >> 1, 1 );
		s_uiHBAOHeight = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Height >> 1, 1 );

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width                = s_uiHBAOWidth;
		desc.Height               = s_uiHBAOHeight;
		desc.MipLevels            = 1;
		desc.ArraySize            = 1;
		desc.Format               = DXGI_FORMAT_R16_FLOAT;
		desc.SampleDesc.Count     = 1;
		desc.SampleDesc.Quality   = 0;
		desc.Usage                = D3D11_USAGE_DEFAULT;
		desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pHBAOTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pHBAOTexture, TNULL, &s_pHBAORTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pHBAOTexture, TNULL, &s_pHBAOSRV ) );

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pHBAOBlurTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pHBAOBlurTexture, TNULL, &s_pHBAOBlurRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pHBAOBlurTexture, TNULL, &s_pHBAOBlurSRV ) );

		D3D11_BUFFER_DESC hbaoCBDesc = {};
		hbaoCBDesc.ByteWidth         = sizeof( HBAOCBuffer );
		hbaoCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		hbaoCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		hbaoCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &hbaoCBDesc, TNULL, &s_pHBAOConstantBuffer ) );

		D3D11_BUFFER_DESC xeGTAOCBDesc = {};
		xeGTAOCBDesc.ByteWidth         = sizeof( XeGTAOCBuffer );
		xeGTAOCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		xeGTAOCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		xeGTAOCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &xeGTAOCBDesc, TNULL, &s_pXeGTAOConstantBuffer ) );

		D3D11_BUFFER_DESC hbaoBlurCBDesc = {};
		hbaoBlurCBDesc.ByteWidth         = sizeof( HBAOBlurCBuffer );
		hbaoBlurCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		hbaoBlurCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		hbaoBlurCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &hbaoBlurCBDesc, TNULL, &s_pHBAOBlurConstantBuffer ) );
	}

	// Dual Kawase blur mip chain (each level halves the previous resolution)
	{
		for ( TINT i = 0; i < KAWASE_MAX_LEVELS; i++ )
		{
			s_uiKawaseWidths[ i ]  = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Width  >> ( i + 2 ), 1 );
			s_uiKawaseHeights[ i ] = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Height >> ( i + 2 ), 1 );

			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width                = s_uiKawaseWidths[ i ];
			desc.Height               = s_uiKawaseHeights[ i ];
			desc.MipLevels            = 1;
			desc.ArraySize            = 1;
			desc.Format               = DXGI_FORMAT_R11G11B10_FLOAT;
			desc.SampleDesc.Count     = 1;
			desc.SampleDesc.Quality   = 0;
			desc.Usage                = D3D11_USAGE_DEFAULT;
			desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pKawaseTextures[ i ] ) );
			DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pKawaseTextures[ i ], TNULL, &s_pKawaseRTVs[ i ] ) );
			DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pKawaseTextures[ i ], TNULL, &s_pKawaseSRVs[ i ] ) );
		}

		D3D11_BUFFER_DESC kawaseCBDesc = {};
		kawaseCBDesc.ByteWidth         = sizeof( KawaseCBuffer );
		kawaseCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		kawaseCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		kawaseCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &kawaseCBDesc, TNULL, &s_pKawaseCBuffer ) );
	}

	// Volumetric Fog (quarter-resolution R11G11B10_FLOAT)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width                = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Width >> 2, 1 );
		desc.Height               = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Height >> 2, 1 );
		desc.MipLevels            = 1;
		desc.ArraySize            = 1;
		desc.Format               = DXGI_FORMAT_R11G11B10_FLOAT;
		desc.SampleDesc.Count     = 1;
		desc.SampleDesc.Quality   = 0;
		desc.Usage                = D3D11_USAGE_DEFAULT;
		desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pVolumetricFogTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pVolumetricFogTexture, TNULL, &s_pVolumetricFogRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pVolumetricFogTexture, TNULL, &s_pVolumetricFogSRV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pVolumetricFogTemporalTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pVolumetricFogTemporalTexture, TNULL, &s_pVolumetricFogTemporalRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pVolumetricFogTemporalTexture, TNULL, &s_pVolumetricFogTemporalSRV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pVolumetricFogHistoryTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pVolumetricFogHistoryTexture, TNULL, &s_pVolumetricFogHistoryRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pVolumetricFogHistoryTexture, TNULL, &s_pVolumetricFogHistorySRV ) );
		s_bVolumetricFogHistoryValid = TFALSE;

		D3D11_BUFFER_DESC fogCBDesc = {};
		fogCBDesc.ByteWidth          = sizeof( VolumetricFogCBuffer );
		fogCBDesc.Usage              = D3D11_USAGE_DYNAMIC;
		fogCBDesc.BindFlags          = D3D11_BIND_CONSTANT_BUFFER;
		fogCBDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &fogCBDesc, TNULL, &s_pVolumetricFogConstantBuffer ) );

		D3D11_BUFFER_DESC fogCompositeCBDesc = {};
		fogCompositeCBDesc.ByteWidth         = sizeof( VolumetricFogCompositeCBuffer );
		fogCompositeCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		fogCompositeCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		fogCompositeCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &fogCompositeCBDesc, TNULL, &s_pVolumetricFogCompositeConstantBuffer ) );

	}

	s_pSkyMaskSampler = CreateSamplerState(
	    D3D11_FILTER_MIN_MAG_MIP_LINEAR,
	    D3D11_TEXTURE_ADDRESS_BORDER,
	    D3D11_TEXTURE_ADDRESS_BORDER,
	    D3D11_TEXTURE_ADDRESS_BORDER,
	    0.0f,
	    0x00000000,
	    0.0f,
	    D3D11_FLOAT32_MAX,
	    1
	);

	s_pPointClampSampler = CreateSamplerState(
	    D3D11_FILTER_MIN_MAG_MIP_POINT,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    0.0f,
	    0x00000000,
	    0.0f,
	    D3D11_FLOAT32_MAX,
	    1
	);

	s_pLinearClampSampler = CreateSamplerState(
	    D3D11_FILTER_MIN_MAG_MIP_LINEAR,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    0.0f,
	    0x00000000,
	    0.0f,
	    D3D11_FLOAT32_MAX,
	    1
	);
}

TBOOL g_bHasGlowObjectsThisFrame = TFALSE;

MEMBER_HOOK( 0x0060b370, ARenderer, ARenderer_RenderMainScene, void, TFLOAT a_flDeltaTime )
{
	// Reset state
	g_bHasGlowObjectsThisFrame = TFALSE;

	auto pSwapChainDesc = remaster::g_pRender->GetSwapChainDesc();

	//-----------------------------------------------------------------------------
	// 1. Shadow pass
	//-----------------------------------------------------------------------------
	auto& csmManager = remaster::g_pRender->GetCSMManager();

	csmManager.UpdateCascades( remaster::g_pRender->GetCurrentContext() );
	remaster::g_pRender->UpdateShadowCBuffer( csmManager.GetShadowCBufferData() );

	if ( remaster::g_bCSMEnabled )
		csmManager.RenderShadowMaps();

	remaster::RenderDynamicGlowShadowMaps();

	//-----------------------------------------------------------------------------
	// 2. Main pass
	//-----------------------------------------------------------------------------

	static constexpr TFLOAT aflSkyMaskClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	remaster::g_pRender->SetRenderTargetView(
	    remaster::g_pRender->GetD3D11RenderTargetView(),
	    remaster::g_pRender->GetD3D11DepthStencilView()
	);

	remaster::g_pRender->GetD3D11DeviceContext()->ClearDepthStencilView( remaster::g_pRender->GetD3D11DepthStencilView(), D3D11_CLEAR_DEPTH, 1.0f, 0 );

	remaster::g_pRender->ClearRenderTarget( remaster::g_pRender->GetD3D11GlowRenderTargetView(), aflSkyMaskClearColor );

	// HACK: make sure depth is not cleared before skymask is generated
	remaster::g_bAllowClearingDepth = TFALSE;
	CallOriginal( a_flDeltaTime );
	remaster::g_bAllowClearingDepth = TTRUE;

	// Resolve MSAA color -> non-MSAA (needed by sky mask shader)
	{
		remaster::g_pRender->GetD3D11DeviceContext()->ResolveSubresource(
		    s_pResolvedColorTexture, 0, remaster::g_pRender->GetD3D11RenderTargetTexture(), 0, DXGI_FORMAT_R8G8B8A8_UNORM );

		remaster::g_pRender->GetD3D11DeviceContext()->ResolveSubresource(
		    s_pResolvedGlowTexture, 0, remaster::g_pRender->GetD3D11GlowRenderTargetTexture(), 0, DXGI_FORMAT_R8G8B8A8_UNORM );
	}

	// Resolve MSAA depth -> R32_FLOAT via fullscreen pass (full-res viewport)
	{
		remaster::g_pRender->DiscardView( s_pResolvedDepthRTV );
		remaster::g_pRender->SetRenderTargetView( s_pResolvedDepthRTV, TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, remaster::g_pRender->GetD3D11DepthStencilSRV() );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetResolveDepthPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::ResolveDepth_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
	}

	// Render HBAO+ style screen-space ambient occlusion and composite it over the scene
	if ( remaster::g_bHBAOEnabled )
	{

		auto             pContext = TSTATICCAST( remaster::RenderContextD3D11, m_pViewport->GetRenderContext() );
		const TMatrix44& proj     = pContext->GetProjectionMatrix();

		// AO + blur run at half resolution; shrink the viewport to match the
		// half-res render targets, then restore it before the full-res composite.
		D3D11_VIEWPORT oHBAOOldVP;
		TUINT          uiHBAONumVP = 1;
		remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiHBAONumVP, &oHBAOOldVP );
		D3D11_VIEWPORT oHBAOHalfVP = oHBAOOldVP;
		oHBAOHalfVP.Width  = TFLOAT( s_uiHBAOWidth );
		oHBAOHalfVP.Height = TFLOAT( s_uiHBAOHeight );
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oHBAOHalfVP );

		remaster::g_pRender->DiscardView( s_pHBAORTV );
		remaster::g_pRender->SetRenderTargetView( s_pHBAORTV, TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedDepthSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pPointClampSampler );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );

		if ( remaster::g_iAOAlgorithm == 1 )
		{
			XeGTAOCBuffer cbData;
			cbData.projection[ 0 ] = proj.m_f11;
			cbData.projection[ 1 ] = proj.m_f22;
			cbData.projection[ 2 ] = proj.m_f31;
			cbData.projection[ 3 ] = proj.m_f32;
			cbData.depthParams[ 0 ] = proj.m_f33;
			cbData.depthParams[ 1 ] = proj.m_f43;
			cbData.depthParams[ 2 ] = pContext->GetProjectionParams().m_fNearClip;
			cbData.depthParams[ 3 ] = pContext->GetProjectionParams().m_fFarClip;
			cbData.params[ 0 ] = remaster::g_flHBAORadius * remaster::g_flHBAOSceneScale;
			cbData.params[ 1 ] = remaster::g_flXeGTAOFalloffRange;
			cbData.params[ 2 ] = remaster::g_flHBAOIntensity;
			cbData.params[ 3 ] = remaster::g_flHBAOPower;
			cbData.bufferSize[ 0 ] = TFLOAT( s_uiHBAOWidth );
			cbData.bufferSize[ 1 ] = TFLOAT( s_uiHBAOHeight );
			cbData.bufferSize[ 2 ] = 1.0f / cbData.bufferSize[ 0 ];
			cbData.bufferSize[ 3 ] = 1.0f / cbData.bufferSize[ 1 ];
			cbData.xeParams[ 0 ] = remaster::g_flXeGTAORadiusMultiplier;
			cbData.xeParams[ 1 ] = remaster::g_flXeGTAOSampleDistributionPower;
			cbData.xeParams[ 2 ] = remaster::g_flXeGTAOThinOccluderCompensation;
			cbData.xeParams[ 3 ] = 0.0f;

			D3D11_MAPPED_SUBRESOURCE mapped;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pXeGTAOConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pXeGTAOConstantBuffer, 0 );

			remaster::g_pRender->PSSetConstantBuffer( 1, s_pXeGTAOConstantBuffer );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetXeGTAOPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::XeGTAO_NoCombos )
			);
		}
		else
		{
			HBAOCBuffer cbData;
			cbData.projection[ 0 ] = proj.m_f11;
			cbData.projection[ 1 ] = proj.m_f22;
			cbData.projection[ 2 ] = proj.m_f31;
			cbData.projection[ 3 ] = proj.m_f32;
			cbData.depthParams[ 0 ] = proj.m_f33;
			cbData.depthParams[ 1 ] = proj.m_f43;
			cbData.depthParams[ 2 ] = pContext->GetProjectionParams().m_fNearClip;
			cbData.depthParams[ 3 ] = pContext->GetProjectionParams().m_fFarClip;
			cbData.params[ 0 ] = remaster::g_flHBAORadius * remaster::g_flHBAOSceneScale;
			cbData.params[ 1 ] = remaster::g_flHBAOBias;
			cbData.params[ 2 ] = remaster::g_flHBAOIntensity;
			cbData.params[ 3 ] = remaster::g_flHBAOPower;
			cbData.bufferSize[ 0 ] = TFLOAT( s_uiHBAOWidth );
			cbData.bufferSize[ 1 ] = TFLOAT( s_uiHBAOHeight );
			cbData.bufferSize[ 2 ] = 1.0f / cbData.bufferSize[ 0 ];
			cbData.bufferSize[ 3 ] = 1.0f / cbData.bufferSize[ 1 ];

			D3D11_MAPPED_SUBRESOURCE mapped;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pHBAOConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pHBAOConstantBuffer, 0 );

			remaster::g_pRender->PSSetConstantBuffer( 1, s_pHBAOConstantBuffer );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetHBAOPlusPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::HBAOPlus_NoCombos )
			);
		}

		remaster::g_pRender->PSSetShaderResource( 0, TNULL );

		const TFLOAT fNearClip = pContext->GetProjectionParams().m_fNearClip;
		const TFLOAT fFarClip  = pContext->GetProjectionParams().m_fFarClip;

		auto fnBlurHBAO = [ fNearClip, fFarClip ]( ID3D11RenderTargetView* a_pRTV, ID3D11ShaderResourceView* a_pInputSRV, TFLOAT a_fDirX, TFLOAT a_fDirY )
		{
			HBAOBlurCBuffer blurData;
			blurData.blurParams[ 0 ] = 1.0f / TFLOAT( s_uiHBAOWidth );
			blurData.blurParams[ 1 ] = 1.0f / TFLOAT( s_uiHBAOHeight );
			blurData.blurParams[ 2 ] = a_fDirX;
			blurData.blurParams[ 3 ] = a_fDirY;
			blurData.depthParams[ 0 ] = fNearClip;
			blurData.depthParams[ 1 ] = fFarClip;
			blurData.depthParams[ 2 ] = remaster::g_flHBAOBlurSharpness;
			blurData.depthParams[ 3 ] = 0.0f;

			D3D11_MAPPED_SUBRESOURCE mappedBlur;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pHBAOBlurConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedBlur );
			TUtil::MemCopy( mappedBlur.pData, &blurData, sizeof( blurData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pHBAOBlurConstantBuffer, 0 );

			remaster::g_pRender->DiscardView( a_pRTV );
			remaster::g_pRender->SetRenderTargetView( a_pRTV, TNULL );
			remaster::g_pRender->PSSetShaderResource( 0, a_pInputSRV );
			remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedDepthSRV );
			remaster::g_pRender->PSSetSamplerState( 0, s_pPointClampSampler );
			remaster::g_pRender->PSSetSamplerState( 1, s_pLinearClampSampler );
			remaster::g_pRender->PSSetConstantBuffer( 1, s_pHBAOBlurConstantBuffer );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetHBAOBlurPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::HBAOBlur_NoCombos )
			);
			remaster::g_pRender->PSSetShaderResource( 0, TNULL );
			remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		};

		fnBlurHBAO( s_pHBAOBlurRTV, s_pHBAOSRV, 1.0f, 0.0f );
		fnBlurHBAO( s_pHBAORTV, s_pHBAOBlurSRV, 0.0f, 1.0f );

		// Restore the full-resolution viewport for the composite pass.
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oHBAOOldVP );

		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 0, s_pHBAOSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->SetBlendEnabled( TFALSE );

		if ( remaster::g_bHBAODebug )
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetHBAOCompositePixelShaderCombo_ps_debug().GetPixelShader( remaster::shadercombos::HBAOComposite_NoCombos )
			);
		}
		else
		{
			remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedColorSRV );
			remaster::g_pRender->PSSetSamplerState( 1, s_pLinearClampSampler );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetHBAOCompositePixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::HBAOComposite_NoCombos )
			);
			remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		}

		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
	}

	// Volumetric fog pass (half-res, raymarched, CSM-shadowed)
	if ( remaster::g_bCSMEnabled && remaster::g_bVolumetricFogEnabled )
	{
		auto             pFogCtx = TSTATICCAST( remaster::RenderContextD3D11, m_pViewport->GetRenderContext() );
		const TMatrix44& proj    = pFogCtx->GetProjectionMatrix();

		TMatrix44 matViewWorld = pFogCtx->GetViewWorldMatrix();
		if ( *(void**)0x007822e0 )
		{
			ACamera* pCamera = CALL_THIS( 0x0045b870, void*, ACamera*, *(void**)0x007822e0 ); // ACameraManager::GetCurrentCamera
			if ( pCamera )
				matViewWorld = pCamera->m_Matrix;
		}

		TMatrix44 matWorldView;
		matWorldView.InvertOrthogonal( matViewWorld );

		// Light direction toward sun in view space
		TVector3 fogLightDir = csmManager.GetLightDirection();
		fogLightDir.Normalize();
		TVector4 fogLightDirWorld( -fogLightDir.x, -fogLightDir.y, -fogLightDir.z, 0.0f );
		TVector4 fogLightDirView;
		TMatrix44::RotateVector( fogLightDirView, matWorldView, fogLightDirWorld );

		TFLOAT fogLightDirLen = TMath::Sqrt(
		    fogLightDirView.x * fogLightDirView.x +
		    fogLightDirView.y * fogLightDirView.y +
		    fogLightDirView.z * fogLightDirView.z
		);
		if ( fogLightDirLen > 0.0001f )
		{
			fogLightDirView.x /= fogLightDirLen;
			fogLightDirView.y /= fogLightDirLen;
			fogLightDirView.z /= fogLightDirLen;
		}

		const auto& shadowData = csmManager.GetShadowCBufferData();

		VolumetricFogCBuffer cbFog = {};
		for ( TINT i = 0; i < 3; i++ )
			TUtil::MemCopy( cbFog.matLightVP[ i ], &shadowData.matLightVP[ i ], sizeof( TMatrix44 ) );
		cbFog.cascadeSplits[ 0 ] = shadowData.cascadeSplits[ 0 ];
		cbFog.cascadeSplits[ 1 ] = shadowData.cascadeSplits[ 1 ];
		cbFog.cascadeSplits[ 2 ] = shadowData.cascadeSplits[ 2 ];
		cbFog.cascadeSplits[ 3 ] = shadowData.cascadeSplits[ 3 ];
		cbFog.shadowBias[ 0 ]    = shadowData.shadowParams[ 0 ];
		cbFog.shadowBias[ 1 ]    = shadowData.shadowParams[ 1 ];
		cbFog.shadowBias[ 2 ]    = 0.0f;
		cbFog.shadowBias[ 3 ]    = 0.0f;
		cbFog.matViewWorld       = matViewWorld;
		cbFog.projection[ 0 ]    = proj.m_f11;
		cbFog.projection[ 1 ]    = proj.m_f22;
		cbFog.projection[ 2 ]    = proj.m_f31;
		cbFog.projection[ 3 ]    = proj.m_f32;
		cbFog.depthParams[ 0 ]   = 0.0f;
		cbFog.depthParams[ 1 ]   = 0.0f;
		cbFog.depthParams[ 2 ]   = pFogCtx->GetProjectionParams().m_fNearClip;
		cbFog.depthParams[ 3 ]   = pFogCtx->GetProjectionParams().m_fFarClip;
		cbFog.lightDirVS[ 0 ]    = fogLightDirView.x;
		cbFog.lightDirVS[ 1 ]    = fogLightDirView.y;
		cbFog.lightDirVS[ 2 ]    = fogLightDirView.z;
		cbFog.lightDirVS[ 3 ]    = 0.0f;
		cbFog.fogColor[ 0 ]      = remaster::g_flVolumetricFogColor[ 0 ];
		cbFog.fogColor[ 1 ]      = remaster::g_flVolumetricFogColor[ 1 ];
		cbFog.fogColor[ 2 ]      = remaster::g_flVolumetricFogColor[ 2 ];
		cbFog.fogColor[ 3 ]      = 1.0f;
		cbFog.fogParams[ 0 ]     = remaster::g_flVolumetricFogDensity;
		cbFog.fogParams[ 1 ]     = remaster::g_flVolumetricFogG;
		cbFog.fogParams[ 2 ]     = remaster::g_flVolumetricFogMaxDist;
		cbFog.fogParams[ 3 ]     = remaster::g_flVolumetricFogIntensity;
		if ( remaster::g_iVolumetricFogCompositeMode == 1 )
			TMath::Clip( cbFog.fogParams[ 3 ], 0.0f, 1.0f );
		cbFog.frameParams[ 0 ]   = TFLOAT( s_uiVolumetricFogFrameIndex & 7 );
		cbFog.frameParams[ 1 ]   = 0.0f;
		cbFog.frameParams[ 2 ]   = 0.0f;
		cbFog.frameParams[ 3 ]   = 0.0f;
		s_uiVolumetricFogFrameIndex++;

		D3D11_MAPPED_SUBRESOURCE fogMapped;
		remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pVolumetricFogConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &fogMapped );
		TUtil::MemCopy( fogMapped.pData, &cbFog, sizeof( cbFog ) );
		remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pVolumetricFogConstantBuffer, 0 );

		D3D11_VIEWPORT oFogOldVP;
		TUINT          uiFogNumVP = 1;
		remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiFogNumVP, &oFogOldVP );
		D3D11_VIEWPORT oFogQuarterVP = oFogOldVP;
		oFogQuarterVP.Width  *= 0.25f;
		oFogQuarterVP.Height *= 0.25f;
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oFogQuarterVP );

		static constexpr TFLOAT aflFogClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
		remaster::g_pRender->DiscardView( s_pVolumetricFogRTV );
		remaster::g_pRender->ClearRenderTarget( s_pVolumetricFogRTV, aflFogClear );
		remaster::g_pRender->SetRenderTargetView( s_pVolumetricFogRTV, TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedDepthSRV );
		remaster::g_pRender->PSSetShaderResource( 1, csmManager.GetShadowSRV() );
		remaster::g_pRender->PSSetSamplerState( 0, s_pPointClampSampler );
		remaster::g_pRender->PSSetSamplerState( 1, csmManager.GetShadowSampler() );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pVolumetricFogConstantBuffer );
		TUINT uiVolumetricFogComboFlags = 0;
		if ( !remaster::g_bDynamicGlowEnabled ||
		     remaster::g_iVolumetricFogCompositeMode == 1
			// TODO: check if any light is actually visible rn
			)
		{
			uiVolumetricFogComboFlags |= remaster::shadercombos::VolumetricFog_NO_DYN_LIGHT;
		}
		else
		{
			remaster::UploadVolumetricDynamicGlowLightsCBuffer();
		}
		const TUINT uiVolumetricFogComboIndex = remaster::shadercombos::GetVolumetricFogComboIndex( uiVolumetricFogComboFlags );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		if ( remaster::g_iVolumetricFogCompositeMode == 1 )
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetVolumetricFogPixelShaderCombo_ps_visibility().GetPixelShader( uiVolumetricFogComboIndex )
			);
		}
		else
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetVolumetricFogPixelShaderCombo_ps_main().GetPixelShader( uiVolumetricFogComboIndex )
			);
		}
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		remaster::g_pRender->PSSetShaderResource( 6, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 2, TNULL );
		remaster::g_pRender->PSSetSamplerState( 6, TNULL );

		VolumetricFogCompositeCBuffer cbFogComposite = {};
		cbFogComposite.depthParams[ 0 ]     = 0.0f;
		cbFogComposite.depthParams[ 1 ]     = 0.0f;
		cbFogComposite.depthParams[ 2 ]     = pFogCtx->GetProjectionParams().m_fNearClip;
		cbFogComposite.depthParams[ 3 ]     = pFogCtx->GetProjectionParams().m_fFarClip;
		cbFogComposite.compositeParams[ 0 ] = ( remaster::g_iVolumetricFogCompositeMode == 1 ) ? 1.0f : 0.12f;
		cbFogComposite.compositeParams[ 1 ] = s_bVolumetricFogHistoryValid ? 1.0f : 0.0f;
		cbFogComposite.compositeParams[ 2 ] = 24.0f;
		cbFogComposite.compositeParams[ 3 ] = 0.0f;

		D3D11_MAPPED_SUBRESOURCE fogCompositeMapped;
		remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pVolumetricFogCompositeConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &fogCompositeMapped );
		TUtil::MemCopy( fogCompositeMapped.pData, &cbFogComposite, sizeof( cbFogComposite ) );
		remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pVolumetricFogCompositeConstantBuffer, 0 );

		// Temporal resolve in half resolution before the full-resolution composite.
		remaster::g_pRender->DiscardView( s_pVolumetricFogTemporalRTV );
		remaster::g_pRender->SetRenderTargetView( s_pVolumetricFogTemporalRTV, TNULL );
		if ( !s_bVolumetricFogHistoryValid )
		{
			remaster::g_pRender->GetD3D11DeviceContext()->CopyResource( s_pVolumetricFogHistoryTexture, s_pVolumetricFogTexture );
			s_bVolumetricFogHistoryValid = TTRUE;
		}
		remaster::g_pRender->PSSetShaderResource( 0, s_pVolumetricFogSRV );
		remaster::g_pRender->PSSetShaderResource( 3, s_pVolumetricFogHistorySRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pVolumetricFogCompositeConstantBuffer );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetVolumetricFogCompositePixelShaderCombo_ps_temporal().GetPixelShader( remaster::shadercombos::VolumetricFogComposite_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetShaderResource( 3, TNULL );

		// Restore viewport, composite fog onto the scene
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oFogOldVP );
		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
		// Re-resolve MSAA color so the fog composite sees the post-AO scene:
		// the HBAO composite drew scene*ao to the MSAA RT after the first resolve,
		// so s_pResolvedColorSRV would otherwise still hold the pre-AO color.
		remaster::g_pRender->GetD3D11DeviceContext()->ResolveSubresource(
		    s_pResolvedColorTexture, 0, remaster::g_pRender->GetD3D11RenderTargetTexture(), 0, DXGI_FORMAT_R8G8B8A8_UNORM );

		// Ping-pong: swap temporal <-> history pointers so next frame's temporal
		// pass reads from what we just wrote, without any GPU copy.
		{
			ID3D11Texture2D*          pTmpTex = s_pVolumetricFogTemporalTexture;
			ID3D11RenderTargetView*   pTmpRTV = s_pVolumetricFogTemporalRTV;
			ID3D11ShaderResourceView* pTmpSRV = s_pVolumetricFogTemporalSRV;
			s_pVolumetricFogTemporalTexture = s_pVolumetricFogHistoryTexture;
			s_pVolumetricFogTemporalRTV     = s_pVolumetricFogHistoryRTV;
			s_pVolumetricFogTemporalSRV     = s_pVolumetricFogHistorySRV;
			s_pVolumetricFogHistoryTexture  = pTmpTex;
			s_pVolumetricFogHistoryRTV      = pTmpRTV;
			s_pVolumetricFogHistorySRV      = pTmpSRV;
		}
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 0, s_pVolumetricFogTemporalSRV );
		remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedColorSRV );
		remaster::g_pRender->PSSetShaderResource( 2, s_pResolvedDepthSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->PSSetSamplerState( 1, s_pLinearClampSampler );
		remaster::g_pRender->PSSetSamplerState( 2, s_pPointClampSampler );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pVolumetricFogCompositeConstantBuffer );

		if ( remaster::g_iVolumetricFogCompositeMode == 1 )
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetVolumetricFogCompositePixelShaderCombo_ps_darken().GetPixelShader( remaster::shadercombos::VolumetricFogComposite_NoCombos )
			);
		}
		else
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetVolumetricFogCompositePixelShaderCombo_ps_additive().GetPixelShader( remaster::shadercombos::VolumetricFogComposite_NoCombos )
			);
		}

		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		remaster::g_pRender->PSSetShaderResource( 2, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
		remaster::g_pRender->ClearStateCache();
	}

	remaster::g_pRender->SetRenderTargetView(
	    remaster::g_pRender->GetD3D11RenderTargetView(),
	    remaster::g_pRender->GetD3D11DepthStencilView()
	);

	// Overlay glow objects captured during the main pass, then bloom them.
	if ( g_bHasGlowObjectsThisFrame )
	{
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedGlowSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetCopyTexturePixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::CopyTexture_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );

		if ( remaster::g_bGlowBloomEnabled )
		{
			D3D11_VIEWPORT oGlowOldViewport;
			TUINT          uiGlowNumViewports = 1;
			remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiGlowNumViewports, &oGlowOldViewport );

			for ( TINT k = 0; k < KAWASE_MAX_LEVELS; k++ )
			{
				remaster::g_pRender->DiscardView( s_pKawaseRTVs[ k ] );
				remaster::g_pRender->ClearRenderTarget( s_pKawaseRTVs[ k ], aflSkyMaskClearColor );
			}

			auto fnKawasePass = [&]( ID3D11RenderTargetView* pDstRTV, ID3D11ShaderResourceView* pSrcSRV,
			                         TUINT uiSrcW, TUINT uiSrcH, TUINT uiDstW, TUINT uiDstH, TBOOL bDownsample, TBOOL bComposite )
			{
				KawaseCBuffer cbData;
				cbData.texelSizeX = 1.0f / (TFLOAT)uiSrcW;
				cbData.texelSizeY = 1.0f / (TFLOAT)uiSrcH;
				cbData.offset     = remaster::g_flGlowBloomKawaseOffset;
				cbData.PADDING    = bComposite ? remaster::g_flGlowBloomIntensity : 0.0f;

				D3D11_MAPPED_SUBRESOURCE mapped;
				remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pKawaseCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
				TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
				remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pKawaseCBuffer, 0 );

				D3D11_VIEWPORT vp = oGlowOldViewport;
				vp.Width          = (TFLOAT)uiDstW;
				vp.Height         = (TFLOAT)uiDstH;
				remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &vp );

				remaster::g_pRender->SetRenderTargetView( pDstRTV, TNULL );
				remaster::g_pRender->PSSetShaderResource( 0, pSrcSRV );
				remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
				remaster::g_pRender->PSSetConstantBuffer( 1, s_pKawaseCBuffer );

				using namespace remaster::shadercombos;
				if ( bDownsample )
					remaster::g_pRender->DrawScreenRectangle( GetDualKawaseDownPixelShaderCombo_ps_main().GetPixelShader( DualKawaseDown_NoCombos ) );
				else if ( bComposite )
					remaster::g_pRender->DrawScreenRectangle( GetGlowBloomCompositePixelShaderCombo_ps_main().GetPixelShader( GlowBloomComposite_NoCombos ) );
				else
					remaster::g_pRender->DrawScreenRectangle( GetDualKawaseUpPixelShaderCombo_ps_main().GetPixelShader( DualKawaseUp_NoCombos ) );

				remaster::g_pRender->PSSetShaderResource( 0, TNULL );
			};

			TINT iNumLevels = remaster::g_iGlowBloomKawaseLevels;
			TMath::Clip( iNumLevels, 1, KAWASE_MAX_LEVELS );

			for ( TINT k = 0; k < iNumLevels; k++ )
			{
				ID3D11ShaderResourceView* pSrcSRV = ( k == 0 ) ? s_pResolvedGlowSRV : s_pKawaseSRVs[ k - 1 ];
				TUINT uiSrcW = ( k == 0 ) ? pSwapChainDesc->BufferDesc.Width : s_uiKawaseWidths[ k - 1 ];
				TUINT uiSrcH = ( k == 0 ) ? pSwapChainDesc->BufferDesc.Height : s_uiKawaseHeights[ k - 1 ];
				fnKawasePass( s_pKawaseRTVs[ k ], pSrcSRV, uiSrcW, uiSrcH, s_uiKawaseWidths[ k ], s_uiKawaseHeights[ k ], TTRUE, TFALSE );
			}

			remaster::g_pRender->SetBlendEnabled( TFALSE );
			for ( TINT k = iNumLevels - 1; k > 0; k-- )
			{
				fnKawasePass(
				    s_pKawaseRTVs[ k - 1 ],
				    s_pKawaseSRVs[ k ],
				    s_uiKawaseWidths[ k ],
				    s_uiKawaseHeights[ k ],
				    s_uiKawaseWidths[ k - 1 ],
				    s_uiKawaseHeights[ k - 1 ],
				    TFALSE,
				    TFALSE
				);
			}

			remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oGlowOldViewport );
			remaster::g_pRender->SetRenderTargetView(
			    remaster::g_pRender->GetD3D11RenderTargetView(),
			    remaster::g_pRender->GetD3D11DepthStencilView()
			);
			remaster::g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE );
			remaster::g_pRender->SetDepthEnabled( TFALSE );
			fnKawasePass(
			    remaster::g_pRender->GetD3D11RenderTargetView(),
			    s_pKawaseSRVs[ 0 ],
			    s_uiKawaseWidths[ 0 ],
			    s_uiKawaseHeights[ 0 ],
			    pSwapChainDesc->BufferDesc.Width,
			    pSwapChainDesc->BufferDesc.Height,
			    TFALSE,
			    TTRUE
			);
			remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
			remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oGlowOldViewport );
		}

		remaster::g_pRender->ClearStateCache();
	}

	// Generate skymask to render sunshafts later
	// Since it's rendered in half resolution, need to adjust viewport
	D3D11_VIEWPORT oOldViewport;
	D3D11_VIEWPORT oNewViewport;
	TUINT          uiNumViewports = 1;
	remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiNumViewports, &oOldViewport );

	oNewViewport = oOldViewport;
	oNewViewport.Width *= 0.5f;
	oNewViewport.Height *= 0.5f;
	remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oNewViewport );

	auto fnRestoreState = [ &oOldViewport ]() {
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oOldViewport );

		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
	};

	// Render the sky mask
	{

		remaster::g_pRender->DiscardView( s_pSkyMaskRenderTargetView );
		remaster::g_pRender->ClearRenderTarget( s_pSkyMaskRenderTargetView, aflSkyMaskClearColor );
		remaster::g_pRender->SetRenderTargetView( s_pSkyMaskRenderTargetView, TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedDepthSRV );
		remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedColorSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pSkyMaskSampler );
		remaster::g_pRender->PSSetSamplerState( 1, s_pSkyMaskSampler );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->DrawScreenRectangle(
			remaster::shadercombos::GetSkyMaskPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::SkyMask_NoCombos )
		);

		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetShaderResource( 1, TNULL );
	}

	// Render the sunshafts
	{
		remaster::g_pRender->DiscardView( s_pSunshaftsRenderTargetView );
		remaster::g_pRender->ClearRenderTarget( s_pSunshaftsRenderTargetView, aflSkyMaskClearColor );
		remaster::g_pRender->SetRenderTargetView(
		    s_pSunshaftsRenderTargetView,
		    TNULL
		);

		// Project sun direction to screen-space UV
		TVector3 lightDir = csmManager.GetLightDirection();
		lightDir.Normalize();

		auto             pContext = TSTATICCAST( remaster::RenderContextD3D11, m_pViewport->GetRenderContext() );
		const TMatrix44& proj     = pContext->GetProjectionMatrix();
		TMatrix44        matViewWorld = pContext->GetViewWorldMatrix();
		if ( *(void**)0x007822e0 )
		{
			ACamera* pCamera = CALL_THIS( 0x0045b870, void*, ACamera*, *(void**)0x007822e0 ); // ACameraManager::GetCurrentCamera
			if ( pCamera )
				matViewWorld = pCamera->m_Matrix;
		}

		TMatrix44 matWorldView;
		matWorldView.InvertOrthogonal( matViewWorld );

		// Sun direction toward the sun in world space, then into the same view space
		// that produced the sky mask/depth textures.
		TVector4 sunDirWorld( -lightDir.x, lightDir.y, -lightDir.z, 0.0f );
		TVector4 sunDirView;
		TMatrix44::RotateVector( sunDirView, matWorldView, sunDirWorld );

		TFLOAT vx = sunDirView.x;
		TFLOAT vy = sunDirView.y;
		TFLOAT vz = sunDirView.z;

		// Sun is behind the camera or effect is disabled - skip the draw entirely
		if ( vz <= 0.0f || !remaster::g_bSunShaftsEnabled )
		{
			fnRestoreState();
			return;
		}

		// D3D viewport convention: keep the signed projection Y scale, then invert
		// NDC Y when converting to texture UV. This must mirror UVToView/ViewToUV
		// helpers used by screen-space effects, otherwise camera pitch changes the
		// apparent sun direction.
		const TFLOAT invZ = 1.0f / vz;
		const TFLOAT ndcX = ( vx * invZ ) * proj.m_f11 + proj.m_f31;
		const TFLOAT ndcY = ( vy * invZ ) * proj.m_f22 + proj.m_f32;
		TFLOAT sunU = ndcX * 0.5f + 0.5f;
		TFLOAT sunV = ( 1.0f - ndcY ) * 0.5f;

		TFLOAT angleFade = ( vz - 0.10f ) / 0.65f;
		TMath::Clip( angleFade, 0.0f, 1.0f );
		angleFade = angleFade * angleFade * ( 3.0f - 2.0f * angleFade );
		angleFade *= angleFade;

		// Upload constant buffer
		{
			SunShaftsCBuffer cbData;
			cbData.vSunPos[ 0 ]   = sunU;
			cbData.vSunPos[ 1 ]   = sunV;
			cbData.fSunAlpha      = remaster::g_flSunShaftsAlpha * angleFade;
			cbData.fRaysLength    = remaster::g_flSunShaftsRaysLength;
			cbData.vRaysTint[ 0 ] = remaster::g_flSunShaftsTint[ 0 ];
			cbData.vRaysTint[ 1 ] = remaster::g_flSunShaftsTint[ 1 ];
			cbData.vRaysTint[ 2 ] = remaster::g_flSunShaftsTint[ 2 ];
			cbData.PADDING        = 0.0f;

			D3D11_MAPPED_SUBRESOURCE mapped;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pSunShaftsConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pSunShaftsConstantBuffer, 0 );

			remaster::g_pRender->PSSetConstantBuffer( 1, s_pSunShaftsConstantBuffer );
		}

		remaster::g_pRender->PSSetShaderResource( 0, s_pSkyMaskShaderResourceView );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetSunShaftsPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::SunShafts_NoCombos )
		);
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );

		// Dual Kawase blur on the sunshafts texture
		{
			TUINT uiHalfW = pSwapChainDesc->BufferDesc.Width  >> 1;
			TUINT uiHalfH = pSwapChainDesc->BufferDesc.Height >> 1;

			for ( TINT k = 0; k < KAWASE_MAX_LEVELS; k++ )
			{
				remaster::g_pRender->DiscardView( s_pKawaseRTVs[ k ] );
				remaster::g_pRender->ClearRenderTarget( s_pKawaseRTVs[ k ], aflSkyMaskClearColor );
			}

			auto fnKawasePass = [&]( ID3D11RenderTargetView* pDstRTV, ID3D11ShaderResourceView* pSrcSRV,
			                         TUINT uiSrcW, TUINT uiSrcH, TUINT uiDstW, TUINT uiDstH, TBOOL bDownsample )
			{
				KawaseCBuffer cbData;
				cbData.texelSizeX = 1.0f / (TFLOAT)uiSrcW;
				cbData.texelSizeY = 1.0f / (TFLOAT)uiSrcH;
				cbData.offset     = remaster::g_flSunShaftsKawaseOffset;
				cbData.PADDING    = 0.0f;

				D3D11_MAPPED_SUBRESOURCE mapped;
				remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pKawaseCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
				TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
				remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pKawaseCBuffer, 0 );

				D3D11_VIEWPORT vp = oOldViewport;
				vp.Width          = (TFLOAT)uiDstW;
				vp.Height         = (TFLOAT)uiDstH;
				remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &vp );

				remaster::g_pRender->SetRenderTargetView( pDstRTV, TNULL );
				remaster::g_pRender->PSSetShaderResource( 0, pSrcSRV );
				remaster::g_pRender->PSSetConstantBuffer( 1, s_pKawaseCBuffer );

				using namespace remaster::shadercombos;
				if ( bDownsample )
					remaster::g_pRender->DrawScreenRectangle( GetDualKawaseDownPixelShaderCombo_ps_main().GetPixelShader( DualKawaseDown_NoCombos ) );
				else
					remaster::g_pRender->DrawScreenRectangle( GetDualKawaseUpPixelShaderCombo_ps_main().GetPixelShader( DualKawaseUp_NoCombos ) );

				remaster::g_pRender->PSSetShaderResource( 0, TNULL );
			};

			TINT iNumLevels = remaster::g_iSunShaftsKawaseLevels;
			TMath::Clip( iNumLevels, 1, KAWASE_MAX_LEVELS );

			// Downsample passes: sunshafts -> level0 -> level1 -> ...
			for ( TINT k = 0; k < iNumLevels; k++ )
			{
				ID3D11ShaderResourceView* pSrcSRV = ( k == 0 ) ? s_pSunshaftsShaderResourceView : s_pKawaseSRVs[ k - 1 ];
				TUINT uiSrcW = ( k == 0 ) ? uiHalfW : s_uiKawaseWidths[ k - 1 ];
				TUINT uiSrcH = ( k == 0 ) ? uiHalfH : s_uiKawaseHeights[ k - 1 ];
				fnKawasePass( s_pKawaseRTVs[ k ], pSrcSRV, uiSrcW, uiSrcH, s_uiKawaseWidths[ k ], s_uiKawaseHeights[ k ], TTRUE );
			}

			// Upsample passes: levelN -> ... -> level0 -> sunshafts
			for ( TINT k = iNumLevels - 1; k >= 0; k-- )
			{
				ID3D11RenderTargetView* pDstRTV = ( k == 0 ) ? s_pSunshaftsRenderTargetView : s_pKawaseRTVs[ k - 1 ];
				TUINT uiDstW = ( k == 0 ) ? uiHalfW : s_uiKawaseWidths[ k - 1 ];
				TUINT uiDstH = ( k == 0 ) ? uiHalfH : s_uiKawaseHeights[ k - 1 ];
				fnKawasePass( pDstRTV, s_pKawaseSRVs[ k ], s_uiKawaseWidths[ k ], s_uiKawaseHeights[ k ], uiDstW, uiDstH, TFALSE );
			}

			remaster::g_pRender->PSSetShaderResource( 0, TNULL );
			remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
		}

		// Restore viewport and render target
		fnRestoreState();

		// Copy the sunshafts to the default render target
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 0, s_pSunshaftsShaderResourceView );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetCopyTexturePixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::CopyTexture_NoCombos )
		);

		remaster::g_pRender->ClearStateCache();
	}
}

MEMBER_HOOK( 0x00608540, AGlowViewport, AGlowViewport_AddGlowObject, AGlowViewport::GlowObject* )
{
	AGlowViewport::GlowObject* pGlowObject = CallOriginal();

	pGlowObject->m_bIsNightLight = TTRUE;
	return pGlowObject;
}

HOOK(0x006119d0, AModelLoader_CreateMaterial, TMaterial*, TINT a_iOffset, const TCHAR* a_szMaterialName)
{
	TMaterial* pMaterial = CallOriginal( a_iOffset, a_szMaterialName );

// 	if ( TStringManager::String8Compare( a_szMaterialName, "starsquad" ) == 0 )
// 		pMaterial->SetFlags( TMaterial::FLAGS_GLOW, TTRUE );

	return pMaterial;
}

void remaster::SetupRenderHooks()
{
	InstallHook<TRenderD3DInterface_Create>();
	InstallHook<TRenderD3DInterface_CreateObject>();
	InstallHook<TRenderD3DInterface_BeginEndScene>();
	InstallHook<TRenderD3DInterface_FlushShaders>();
	InstallHook<TD3DAdapter_Mode_Device_SupportsVSConstants>();
	InstallHook<ARenderer_RenderMainScene>();
	InstallHook<RenderCellMeshWin>();
	InstallHook<RenderCellMeshDefault>();
	InstallHook<AModelLoader_LoadWorldMeshTRB_Shadow>();
	InstallHook<AGlowViewport_AddGlowObject>();
	InstallHook<AModelLoader_CreateMaterial>();
	
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
