#include "pch.h"
#include "SkinShader.h"
#include "GameSettings.h"
#include "MaterialParams.h"
#include "SkinMaterial.h"
#include "SkinMesh.h"
#include "WorldShader.h"
#include "Resource/ClassPatcher.h"
#include "Resource/TextureResource.h"

#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "RenderContentDX11.h"
#include "CSM/CSMManager.h"
#include "CubemapAnchors.h"
#include "SkyCube.h"
#include "RenderParams.h"
#include "Generated/SkinShaderCombos.h"
#include "Generated/ShadowDepthShaderCombos.h"

#include <Render/TTMDWin.h>
#include <Render/TRenderPacket.h>
#include <Platform/DX8/TRenderInterface_DX8.h>
#include <Platform/DX8/TRenderContext_DX8.h>
#include <Platform/DX8/TVertexBlockResource_DX8.h>
#include <Platform/DX8/TVertexPoolResource_DX8.h>
#include <Platform/DX8/TIndexBlockResource_DX8.h>
#include <Platform/DX8/TIndexPoolResource_DX8.h>

#include <BYardSDK/AGlowViewport.h>

#include "LightManager.h"

#include <AHooks.h>
#include <HookHelpers.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::SkinShaderDX11, 0x0079a648 );

MEMBER_HOOK( 0x005f46a0, ASkinShaderHAL, ASkinShaderHAL_Constructor, remaster::SkinShaderDX11* )
{
	TFree( this );
	return new remaster::SkinShaderDX11();
}

void remaster::SetupRenderHooks_SkinShader()
{
	InstallHook<ASkinShaderHAL_Constructor>();
}

remaster::SkinShaderDX11::SkinShaderDX11()
    : m_pBoneCBuffer( TNULL )
{
	// Set Singleton
	*(ASkinShader**)( 0x0079a4f8 ) = this;
}

remaster::SkinShaderDX11::~SkinShaderDX11()
{

	if ( m_pBoneCBuffer )
	{
		m_pBoneCBuffer->Release();
		m_pBoneCBuffer = TNULL;
	}
}

void remaster::SkinShaderDX11::Flush()
{
	if ( IsValidated() )
	{
		g_pRender->SetBlendEnabled( TTRUE );

		g_pRender->SetZMode( TTRUE, D3D11_COMPARISON_LESS_EQUAL, D3D11_DEPTH_WRITE_MASK_ALL );

		g_pRender->SetCullMode( m_bRenderEnvMap ? D3D11_CULL_BACK : D3D11_CULL_FRONT );
		m_aOrderTables[ 0 ].Render();

		//pDevice->SetRenderState( D3DRS_FOGENABLE, 0 );
		g_pRender->SetAlphaToCoverageEnabled( TTRUE );
	}

	BaseClass::Flush();
}

static TFLOAT s_flFogDensity = 0.0f;

void remaster::SkinShaderDX11::StartFlush()
{
	m_oWorldViewMatrix = g_pRender->GetCurrentContext()->GetWorldViewMatrix();
	m_oViewWorldMatrix.Invert( m_oWorldViewMatrix );

	if ( g_pCSMManager && g_pCSMManager->IsRenderingShadowPass() )
	{
		g_pRender->SetDepthEnabled( TTRUE );
		g_pRender->SetDepthWrite( TTRUE );
		g_pRender->SetBlendEnabled( TFALSE );
		g_pRender->SetAlphaToCoverageEnabled( TFALSE );
		g_pRender->SetCullMode( D3D11_CULL_FRONT );
		return;
	}

	g_pRender->SetBlendEnabled( TTRUE );
	g_pRender->SetCullMode( m_bRenderEnvMap ? D3D11_CULL_BACK : D3D11_CULL_FRONT );
	g_pRender->SetAlphaToCoverageEnabled( TTRUE );

	g_pRender->SetZMode( TTRUE, D3D11_COMPARISON_LESS_EQUAL, D3D11_DEPTH_WRITE_MASK_ALL );

	if ( g_bInMainScenePass && GameSettings::IsCSMEnabled() && g_pCSMManager && g_flShadowIntensity > 0.0f )
	{
		g_pRender->PSSetShaderResource( 5, g_pCSMManager->GetShadowSRV() );
		g_pRender->PSSetSamplerState( 5, g_pCSMManager->GetShadowSampler() );
		g_pRender->PSSetConstantBuffer( 1, g_pRender->GetShadowConstantBuffer() );

		// Animated cloud shadow map (t9/s3), sampled by world XZ in SampleShadow.
		if ( GameSettings::AreCloudShadowsEnabled() )
		{
			g_pRender->PSSetShaderResource( 9, g_pCloudShadowSRV );
			g_pRender->PSSetSamplerState( 3, g_pCloudShadowSampler );
		}
	}

	// bind the reflection cube (last frame) + linear-clamp sampler for env specular; skipped during the capture
	if ( !g_bReflectionCaptureActive && g_oSkyCubeBlend.pSRVTo )
	{
		g_pRender->PSSetShaderResource( 11, g_oSkyCubeBlend.pSRVTo );   // active ("to") cube
		g_pRender->PSSetShaderResource( 12, g_oSkyCubeBlend.pSRVFrom ); // outgoing ("from") cube (cross-fade)
		g_pRender->PSSetSamplerState( 4, SAMPLER_LINEAR_CLAMP );
	}

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	s_flFogDensity                      = dx11::CalculateFogDensity(
        pCurrentContext->m_fFogDistanceStart,
        pCurrentContext->m_fFogDistanceEnd
    );
}

void remaster::SkinShaderDX11::EndFlush()
{
	g_pRender->SetCullMode( D3D11_CULL_NONE );

	if ( g_pCSMManager && g_pCSMManager->IsRenderingShadowPass() )
		return;

	g_pRender->PSSetShaderResource( 1, TNULL );
	g_pRender->PSSetShaderResource( 2, TNULL );
	g_pRender->PSSetShaderResource( 3, TNULL );
	g_pRender->PSSetShaderResource( 4, TNULL );
	g_pRender->PSSetShaderResource( 5, TNULL );
	g_pRender->PSSetShaderResource( 9, TNULL );
	g_pRender->PSSetShaderResource( 10, TNULL );
	g_pRender->PSSetShaderResource( 11, TNULL );
	g_pRender->PSSetShaderResource( 12, TNULL );
	g_pRender->PSSetConstantBuffer( 2, TNULL );

	// Release the wind roughness map from the vertex stage (bound only for wind draws)
	g_pRender->VSSetShaderResource( 8, TNULL );
}

TBOOL remaster::SkinShaderDX11::Create()
{
	m_aOrderTables[ 0 ].Create( this, -390 );
	m_aOrderTables[ 1 ].Create( this, 1000 );
	m_aOrderTables[ 2 ].Create( this, -400 );
	return BaseClass::Create();
}

TBOOL remaster::SkinShaderDX11::Validate()
{
	if ( !ShaderWarmup_IsComplete() )
		return TFALSE;

	if ( IsValidated() )
		return TTRUE;

	dx11::ShaderCombo& rSkinVSCombo       = shadercombos::GetSkinVertexShaderCombo_vs_main();
	dx11::ShaderCombo& rSkinPSCombo       = shadercombos::GetSkinPixelShaderCombo_ps_main();
	dx11::ShaderCombo& rSkinShadowVSCombo = shadercombos::GetShadowDepthVertexShaderCombo_vs_main_skin();
	dx11::ShaderCombo& rSkinShadowPSCombo = shadercombos::GetShadowDepthPixelShaderCombo_ps_main();

	D3D11_INPUT_ELEMENT_DESC aInputElements[] = {
		{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "NORMAL", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "BLENDWEIGHT", .SemanticIndex = 0, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "BLENDINDICES", .SemanticIndex = 0, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "TEXCOORD", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "TANGENT", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .InputSlot = 1, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
	};

	ID3D11InputLayout* pSkinInputLayout = TNULL;
	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aInputElements,
	        TARRAYSIZE( aInputElements ),
	        rSkinVSCombo.GetBlob( 0 )->GetBufferPointer(),
	        rSkinVSCombo.GetBlob( 0 )->GetBufferSize(),
	        &pSkinInputLayout
	    )
	);

	ID3D11InputLayout* pShadowInputLayout = TNULL;
	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aInputElements,
	        TARRAYSIZE( aInputElements ),
	        rSkinShadowVSCombo.GetBlob( 0 )->GetBufferPointer(),
	        rSkinShadowVSCombo.GetBlob( 0 )->GetBufferSize(),
	        &pShadowInputLayout
	    )
	);

	TASSERT( shadercombos::CreateSkinShaderPipelines( rSkinVSCombo, &rSkinPSCombo, pSkinInputLayout, m_vecSkinPipelines, "Skin" ) );
	TASSERT( shadercombos::CreateShadowDepthShaderPipelines( rSkinShadowVSCombo, &rSkinShadowPSCombo, pShadowInputLayout, m_vecSkinShadowPipelines, "Skin_Shadow" ) );

	// HACK: Disable pixel shader manually, because generator doesn't
	const TUINT uiAlphaTestBit = shadercombos::GetShadowDepthComboIndex( shadercombos::ShadowDepth_ALPHATEST );
	for ( TINT i = 0; i < m_vecSkinShadowPipelines.Size(); i++ )
	{
		if ( !( TUINT( i ) & uiAlphaTestBit ) )
			m_vecSkinShadowPipelines[ i ].ppPixelShader = TNULL;
	}

	if ( !m_pBoneCBuffer )
	{
		D3D11_BUFFER_DESC boneDesc    = {};
		boneDesc.ByteWidth            = BONE_GPU_STRIDE * MAX_SKIN_BONES;
		boneDesc.Usage                = D3D11_USAGE_DYNAMIC;
		boneDesc.BindFlags            = D3D11_BIND_CONSTANT_BUFFER;
		boneDesc.CPUAccessFlags       = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( g_pRender->GetD3D11Device()->CreateBuffer( &boneDesc, TNULL, &m_pBoneCBuffer ) );
	}

	return BaseClass::Validate();
}

void remaster::SkinShaderDX11::Invalidate()
{
	BaseClass::Invalidate();
}

TBOOL remaster::SkinShaderDX11::TryInvalidate()
{
	Invalidate();
	return TTRUE;
}

TBOOL remaster::SkinShaderDX11::TryValidate()
{
	Validate();
	return TTRUE;
}

const remaster::RenderDX11::ShaderPipelineState& remaster::SkinShaderDX11::GetSkinPipeline( TBOOL a_bBakedLighting, TBOOL a_bDynLighting, TBOOL a_bIsAnimated, TBOOL a_bHasMaps, TBOOL a_bWind, TBOOL a_bParallax, TBOOL a_bAlphaTest ) const
{
	TUINT uiComboFlags = 0;

	if ( a_bAlphaTest )
		uiComboFlags |= shadercombos::Skin_ALPHATEST;

	if ( a_bHasMaps )
		uiComboFlags |= shadercombos::Skin_MATERIAL_MAPS;

	if ( a_bWind )
		uiComboFlags |= shadercombos::Skin_WIND;

	if ( a_bParallax )
		uiComboFlags |= shadercombos::Skin_PARALLAX;

	if ( GameSettings::AreCloudShadowsEnabled() )
		uiComboFlags |= shadercombos::Skin_CLOUD_SHADOWS;

	if ( a_bBakedLighting )
		uiComboFlags |= shadercombos::Skin_BAKED_LIGHTING;

	if ( a_bIsAnimated )
		uiComboFlags |= shadercombos::Skin_ANIMATED;

	if ( !g_bInMainScenePass || !GameSettings::IsCSMEnabled() || !g_pCSMManager || g_flShadowIntensity <= 0.0f )
		uiComboFlags |= shadercombos::Skin_NO_CSM;

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	if ( !pCurrentContext->IsFogEnabled() || s_flFogDensity <= 0.0f )
		uiComboFlags |= shadercombos::Skin_NO_FOG;

	if ( !GameSettings::AreDynamicLightsEnabled() || !a_bDynLighting )
		uiComboFlags |= shadercombos::Skin_NO_DYN_LIGHT;

	return m_vecSkinPipelines[ shadercombos::GetSkinComboIndex( uiComboFlags ) ];
}

const remaster::RenderDX11::ShaderPipelineState& remaster::SkinShaderDX11::GetShadowPipeline( TBOOL a_bIsAnimated, TBOOL a_bWind, TBOOL a_bAlphaTest ) const
{
	TUINT uiComboFlags = 0;

	if ( a_bIsAnimated )
		uiComboFlags |= shadercombos::ShadowDepth_ANIMATED;

	if ( a_bWind )
		uiComboFlags |= shadercombos::ShadowDepth_WIND;

	if ( a_bAlphaTest )
		uiComboFlags |= shadercombos::ShadowDepth_ALPHATEST;

	return m_vecSkinShadowPipelines[ shadercombos::GetShadowDepthComboIndex( uiComboFlags ) ];
}

static void UploadBonePalette( ID3D11Buffer* a_pBoneCBuffer, Toshi::TSkeletonInstance* a_pSkeletonInstance, ASkinSubMesh* a_pSubMesh )
{
	remaster::RenderDX11* pRender = remaster::g_pRender;

	D3D11_MAPPED_SUBRESOURCE mapped;
	pRender->GetD3D11DeviceContext()->Map( a_pBoneCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
	TFLOAT* pDst = static_cast<TFLOAT*>( mapped.pData );
	for ( TUINT k = 0; k < a_pSubMesh->uiNumBones; k++, pDst += 12 )
	{
		const TFLOAT* pSrc = &a_pSkeletonInstance->GetBone( a_pSubMesh->aBones[ k ] ).m_Transform.m_f11;
		for ( TINT j = 0; j < 3; j++ )
		{
			pDst[ j * 4 + 0 ] = pSrc[ 0 * 4 + j ];
			pDst[ j * 4 + 1 ] = pSrc[ 1 * 4 + j ];
			pDst[ j * 4 + 2 ] = pSrc[ 2 * 4 + j ];
			pDst[ j * 4 + 3 ] = pSrc[ 3 * 4 + j ];
		}
	}
	pRender->GetD3D11DeviceContext()->Unmap( a_pBoneCBuffer, 0 );

	pRender->VSSetConstantBuffer( 1, a_pBoneCBuffer );
}

void remaster::SkinShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
	if ( !a_pRenderPacket || !a_pRenderPacket->GetMesh() ) return;

	TPROFILER_SCOPE();
	RenderImmediate( a_pRenderPacket );
}

void remaster::SkinShaderDX11::RenderImmediate( Toshi::TRenderPacket* a_pRenderPacket )
{
	TPROFILER_SCOPE();

	if ( !a_pRenderPacket || !a_pRenderPacket->GetMesh() ) return;

	TSkeletonInstance*  pSkeletonInstance = a_pRenderPacket->GetSkeletonInstance();
	RenderContextD3D11* pCurrentContext   = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	SkinMesh*           pMesh             = TSTATICCAST( SkinMesh, a_pRenderPacket->GetMesh() );
	SkinMaterial*       pMaterial         = TSTATICCAST( SkinMaterial, a_pRenderPacket->GetMaterial() );

	const TBOOL bIsAnimated = pSkeletonInstance->IsAnyAnimationPlaying();

	if ( g_pCSMManager && g_pCSMManager->IsRenderingShadowPass() )
	{
		auto pCBuffer = g_pRender->GetDepthPassConstantBuffer();

		// Match the main pass's wind so the shadow silhouette sways; needs a roughness map for the
		// wind-strength (blue) channel in the shadow VS
		const remaster::MaterialParams* pShadowParams = pMaterial->GetMaterialParams();
		const TBOOL bWind = GameSettings::IsWindEnabled() && pShadowParams && pShadowParams->bWind && pShadowParams->pRoughnessMap;

		// Explicit MaterialParams override wins, otherwise auto from the diffuse alpha mask
		const TBOOL bShadowAlphaTest = ( pShadowParams && pShadowParams->iShadowAlphaTest >= 0 )
		    ? pShadowParams->iShadowAlphaTest != 0
		    : TextureResource_HasTransparency( pMaterial->GetTexture() );

		g_pRender->SetShaderPipelineState( GetShadowPipeline( bIsAnimated, bWind, bShadowAlphaTest ) );

		// Upload MVP (+ cascade, + wind params) to the depth-pass constant buffer at b0
		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			g_pRender->GetD3D11DeviceContext()->Map( pCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );

			TSTATICCAST( TMatrix44, mapped.pData )->Multiply( g_pCSMManager->GetCurrentLightProjection(), a_pRenderPacket->GetModelViewMatrix() );

			// Slot 4 = cascade, slot 5 = wind dir/strength/time, slot 6 = [windMin, windMax]
			TVector4* pExtra = TREINTERPRETCAST( TVector4*, TSTATICCAST( TMatrix44, mapped.pData ) + 1 );
			pExtra[ 0 ] = TVector4( TFLOAT( g_pCSMManager->GetCurrentCascade() ), 0.0f, 0.0f );
			if ( bWind )
			{
				pExtra[ 1 ] = TVector4( g_flWindDir[ 0 ], g_flWindDir[ 1 ], g_flWindStrength, g_flWindTime );
				pExtra[ 2 ] = TVector4( pShadowParams->fWindMin, pShadowParams->fWindMax, 0.0f, 0.0f );
			}

			g_pRender->GetD3D11DeviceContext()->Unmap( pCBuffer, 0 );
		}

		if ( bWind )
		{
			g_pRender->VSSetShaderResource( 8, (ID3D11ShaderResourceView*)pShadowParams->pRoughnessMap );
			g_pRender->VSSetSamplerState( 0, SAMPLER_LINEAR_WRAP );
		}

		TVertexPoolResource* pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
		TVALIDPTR( pVertexPool );

		for ( TINT i = 0; i < pMesh->GetNumSubMeshes(); i++ )
		{
			ASkinSubMesh* pSubMesh = pMesh->GetSubMesh( i );

			TIndexPoolResource* pIndexPool = TSTATICCAST( TIndexPoolResource, pSubMesh->pIndexPool );
			TVALIDPTR( pIndexPool );

			TVertexBlockResource::HALBuffer vertexBuffer;
			CALL_THIS( 0x006d6660, TVertexPoolResource*, TBOOL, pVertexPool, TVertexBlockResource::HALBuffer&, vertexBuffer ); // pVertexPool->GetHALBuffer( &vertexBuffer );

			TIndexBlockResource::HALBuffer indexBuffer;
			CALL_THIS( 0x006d6180, TIndexPoolResource*, TBOOL, pIndexPool, TIndexBlockResource::HALBuffer&, indexBuffer ); // pIndexPool->GetHALBuffer( &indexBuffer );

			if ( bIsAnimated )
				UploadBonePalette( m_pBoneCBuffer, pSkeletonInstance, pSubMesh );

			g_pRender->DrawIndexed(
			    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
			    pIndexPool->GetNumIndices(),
			    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
			    indexBuffer.uiIndexOffset,
			    DXGI_FORMAT_R16_UINT,
			    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
			    sizeof( TTMDWin::Vertex ),
			    vertexBuffer.uiVertexOffset,
			    pCBuffer
			);
		}
		return;
	}

	const TFLOAT flPacketAlpha     = a_pRenderPacket->GetAlpha();
	const TBOOL  bUseBakedLighting = pMaterial->IsHDLighting() && pMaterial->HasLighting1Tex() && pMaterial->HasLighting2Tex();
	const TBOOL  bHasDynLight      = GameSettings::AreDynamicLightsEnabled() && RenderPacketHasDynamicLights( a_pRenderPacket );

	const remaster::MaterialParams* pSpecParams = pMaterial->GetMaterialParams();
	const TBOOL bHasMaps = pSpecParams && ( pSpecParams->pNormalMap || pSpecParams->pRoughnessMap || pSpecParams->pMetallicMap );

	// Wind reads the roughness map's blue channel in the VS, so it needs both the per-material
	// opt-in and a roughness map to sample. Master-gated by GameSettings::IsWindEnabled()
	const TBOOL bWind = GameSettings::IsWindEnabled() && pSpecParams && pSpecParams->bWind && pSpecParams->pRoughnessMap;

	// Parallax occlusion mapping compiles in only for meshes that ship a height map
	const TBOOL bParallax = pSpecParams && pSpecParams->pHeightMap;

	// Alpha test only when the texture isn't opaque
	const TBOOL bAlphaTest = !remaster::TextureResource_IsOpaque( pMaterial->GetTexture() );

	const TBOOL bIsBlending = pMaterial->GetBlendMode() != 0 || flPacketAlpha < 1.0f || pMaterial->IsBlending();
	g_pRender->SetBlendEnabled( bIsBlending );

	g_pRender->SetShaderPipelineState( GetSkinPipeline( bUseBakedLighting, bHasDynLight, bIsAnimated, bHasMaps, bWind, bParallax, bAlphaTest ) );

	if ( bUseBakedLighting )
	{
		g_pRender->PSSetShaderResource( 1, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_0 )->GetD3DTexture() ) );
		g_pRender->PSSetShaderResource( 2, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_1 )->GetD3DTexture() ) );
		g_pRender->PSSetShaderResource( 3, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_2 )->GetD3DTexture() ) );
		g_pRender->PSSetShaderResource( 4, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_3 )->GetD3DTexture() ) );
		g_pRender->PSSetSamplerState( 1, 5 );
	}

	TMatrix44 matModel;
	matModel.Multiply( pCurrentContext->GetViewWorldMatrix(), a_pRenderPacket->GetModelViewMatrix() );

	TFLOAT   fLightDirX   = -a_pRenderPacket->GetLightDirection().x;
	TFLOAT   fLightDirY   = -a_pRenderPacket->GetLightDirection().y;
	TFLOAT   fLightDirZ   = -a_pRenderPacket->GetLightDirection().z;
	TVector4 vLightColour = a_pRenderPacket->GetLightColour();

	TMatrix44 oModelView = a_pRenderPacket->GetModelViewMatrix();
	TMatrix44 oWorldModelView;

	for ( TINT i = 0; i < 4; i++ )
	{
		oWorldModelView.AsBasisVector4( i ).x = oModelView.m_f11 * m_oWorldViewMatrix.AsBasisVector3( i ).x + oModelView.m_f12 * m_oWorldViewMatrix.AsBasisVector3( i ).y + oModelView.m_f13 * m_oWorldViewMatrix.AsBasisVector3( i ).z;
		oWorldModelView.AsBasisVector4( i ).y = oModelView.m_f21 * m_oWorldViewMatrix.AsBasisVector3( i ).x + oModelView.m_f22 * m_oWorldViewMatrix.AsBasisVector3( i ).y + oModelView.m_f23 * m_oWorldViewMatrix.AsBasisVector3( i ).z;
		oWorldModelView.AsBasisVector4( i ).z = oModelView.m_f31 * m_oWorldViewMatrix.AsBasisVector3( i ).x + oModelView.m_f32 * m_oWorldViewMatrix.AsBasisVector3( i ).y + oModelView.m_f33 * m_oWorldViewMatrix.AsBasisVector3( i ).z;
		oWorldModelView.AsBasisVector4( i ).w = oModelView.m_f14 * m_oWorldViewMatrix.AsBasisVector3( i ).x + oModelView.m_f24 * m_oWorldViewMatrix.AsBasisVector3( i ).y + oModelView.m_f34 * m_oWorldViewMatrix.AsBasisVector3( i ).z;
	}

	TVector3 vLightDirWorld;
	vLightDirWorld.x = oWorldModelView.m_f11 * fLightDirX + oWorldModelView.m_f21 * fLightDirY + oWorldModelView.m_f31 * fLightDirZ;
	vLightDirWorld.y = oWorldModelView.m_f12 * fLightDirX + oWorldModelView.m_f22 * fLightDirY + oWorldModelView.m_f32 * fLightDirZ;
	vLightDirWorld.z = oWorldModelView.m_f13 * fLightDirX + oWorldModelView.m_f23 * fLightDirY + oWorldModelView.m_f33 * fLightDirZ;
	vLightDirWorld.Normalize();

	TVector4  vUpAxis;
	TMatrix44 oViewModel;
	oViewModel.Invert( a_pRenderPacket->GetModelViewMatrix() );
	vUpAxis.Negate3( oViewModel.AsBasisVector4( 2 ) );

	TFLOAT flShadeCoeff = a_pRenderPacket->GetShadeCoeff() * ( 1.0f / 255.0f );
	if ( bUseBakedLighting && pMaterial->GetSomeTexture() )
		flShadeCoeff = flShadeCoeff - 0.3f;

	TVector4 vLightingLerp   = TVector4( 1.0f - flShadeCoeff, 1.0f - flShadeCoeff, 1.0f - flShadeCoeff, 0.0f );
	TVector4 vAmbientColor   = a_pRenderPacket->GetAmbientColour();
	vAmbientColor.w          = a_pRenderPacket->GetAlpha();
	TVector4 vLightDirection = TVector4( vLightDirWorld.x, vLightDirWorld.y, vLightDirWorld.z, 0.0f );

	TINT iAlphaRef = pMaterial->IsBlending() ? 1 : 128;
	TINT iAlpha    = TINT( a_pRenderPacket->GetAlpha() * 128.0f );
	if ( iAlphaRef < iAlpha ) iAlpha = iAlphaRef;
	vLightColour.w = iAlpha * ( 1.0f / 255.0f );

	// Blend enable
	if ( pMaterial->GetBlendMode() != 0 || flPacketAlpha < 1.0f )
		g_pRender->SetBlendEnabled( TTRUE );
	else
		g_pRender->SetBlendEnabled( TFALSE );

	// Fog settings (per-pass, b4)
	TVector4 vFogColor = pCurrentContext->m_FogColor;
	vFogColor.w        = s_flFogDensity;
	g_pRender->PassBufferSetVec4( PASSBUF_FOG_PARAMS, TVector4( pCurrentContext->m_fFogDistanceStart, pCurrentContext->m_fFogDistanceEnd, 0.0f, 0.0f ) );
	g_pRender->PassBufferSetVec4( PASSBUF_FOG_COLOR, vFogColor );

	// Only the model matrix ships per draw; the clip transform (world * pp_matViewProj) is refreshed at pass boundaries
	g_pRender->UpdatePassViewProj( pCurrentContext->GetProjectionMatrix(), pCurrentContext->GetWorldViewMatrix() );
	g_pRender->VSBufferSetMat4( 0, matModel );
	g_pRender->VSBufferSetVec4( 4, vAmbientColor );
	g_pRender->VSBufferSetVec4( 5, vLightColour );
	g_pRender->VSBufferSetVec4( 6, vLightDirection );
	g_pRender->VSBufferSetVec4( 7, vUpAxis );
	g_pRender->VSBufferSetVec4( 8, vLightingLerp );

	const TVector3 camPos = pCurrentContext->GetViewWorldMatrix().GetTranslation3();
	g_pRender->PassBufferSetVec4( PASSBUF_CAMERA_POS, TVector4( camPos.x, camPos.y, camPos.z, 0.0f ) );

	// Per-material normal/roughness/height/metallic map SRVs at t7/t8/t10/t13 (t1-t4 are the
	// baked lighting); presence flags live in the material record (b5), maps sample s0
	if ( pSpecParams && pSpecParams->pNormalMap )
		g_pRender->PSSetShaderResource( 7, (ID3D11ShaderResourceView*)pSpecParams->pNormalMap );
	if ( pSpecParams && pSpecParams->pRoughnessMap )
		g_pRender->PSSetShaderResource( 8, (ID3D11ShaderResourceView*)pSpecParams->pRoughnessMap );
	if ( pSpecParams && pSpecParams->pMetallicMap )
		g_pRender->PSSetShaderResource( 13, (ID3D11ShaderResourceView*)pSpecParams->pMetallicMap );
	if ( bParallax )
		g_pRender->PSSetShaderResource( 10, (ID3D11ShaderResourceView*)pSpecParams->pHeightMap );

	// All static material values come from the immutable material buffer (b5)
	remaster::BindMaterialConstants( pSpecParams, remaster::MATBUF_DEFAULT_SKIN );

	// Per-pass env-specular vec4: [intensity, cube max mip, capture-active mask (kills
	// metallic/parallax), tangent-debug]. Must match the world shader's write
	const TFLOAT flEnvIntensity = ( g_bReflectionCaptureActive || !GameSettings::IsEnvSpecularEnabled() ) ? 0.0f : 1.0f;
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_SPECULAR,
	    TVector4( flEnvIntensity, TFLOAT( g_iSkyCubeMaxMip ), g_bReflectionCaptureActive ? 1.0f : 0.0f, remaster::g_bDebugTangents ? 1.0f : 0.0f ) );

	// CSM sun direction (per-pass), engine (-x,+y,-z) mapping; drives sun specular only so the
	// highlight agrees with the CSM shadows (diffuse still uses the per-packet light dir)
	const TVector3 sunDir = g_pCSMManager ? g_pCSMManager->GetLightDirection() : TVector3( 0.0f, -1.0f, 0.0f );
	g_pRender->PassBufferSetVec4( PASSBUF_SUN_DIRECTION, TVector4( -sunDir.x, sunDir.y, -sunDir.z, 0.0f ) );

	// Env-specular cross-fade (per-pass): "to" cube (box+probe, w=blend) + outgoing "from" cube,
	// from the capture blend state so each probe centre matches where its cube was rendered (else
	// reflections swim). Steady state: blend = 1, "to" only
	const remaster::SkyCubeBlendState& rBlend = remaster::g_oSkyCubeBlend;
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_PARALLAX, TVector4( rBlend.vBoxTo.x, rBlend.vBoxTo.y, rBlend.vBoxTo.z, 0.0f ) );
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_PROBE_POS, TVector4( rBlend.vProbeTo.x, rBlend.vProbeTo.y, rBlend.vProbeTo.z, rBlend.flBlend ) );
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_PARALLAX2, TVector4( rBlend.vBoxFrom.x, rBlend.vBoxFrom.y, rBlend.vBoxFrom.z, 0.0f ) );
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_PROBE_POS2, TVector4( rBlend.vProbeFrom.x, rBlend.vProbeFrom.y, rBlend.vProbeFrom.z, 0.0f ) );

	// Wind (vertex stage): deformation samples the roughness map's blue channel in the VS, so bind
	// SRV+sampler there. Direction/strength/time per-pass; [windMin,windMax] remap rides in the material record
	g_pRender->PassBufferSetVec4( PASSBUF_WIND_PARAMS, TVector4( g_flWindDir[ 0 ], g_flWindDir[ 1 ], g_flWindStrength, g_flWindTime ) );
	if ( bWind )
	{
		g_pRender->VSSetShaderResource( 8, (ID3D11ShaderResourceView*)pSpecParams->pRoughnessMap );
		g_pRender->VSSetSamplerState( 0, SAMPLER_LINEAR_WRAP );
	}

	if ( bHasDynLight ) UploadDynamicLights( a_pRenderPacket );

	// Per-cell static point light indices into the global static-light cbuffer (b3).
	g_pRender->GetLightManager().UploadCellStaticLightIndices( a_pRenderPacket, 9, 11 );

	// Set vertices
	TVertexPoolResource* pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
	TVALIDPTR( pVertexPool );

	for (TINT i = 0; i < pMesh->GetNumSubMeshes(); i++)
	{
		ASkinSubMesh* pSubMesh = pMesh->GetSubMesh( i );

		TIndexPoolResource* pIndexPool = TSTATICCAST( TIndexPoolResource, pSubMesh->pIndexPool );
		TVALIDPTR( pIndexPool );

		TVertexBlockResource::HALBuffer vertexBuffer;
		CALL_THIS( 0x006d6660, TVertexPoolResource*, TBOOL, pVertexPool, TVertexBlockResource::HALBuffer&, vertexBuffer ); // pVertexPool->GetHALBuffer( &vertexBuffer );

		TIndexBlockResource::HALBuffer indexBuffer;
		CALL_THIS( 0x006d6180, TIndexPoolResource*, TBOOL, pIndexPool, TIndexBlockResource::HALBuffer&, indexBuffer ); // pIndexPool->GetHALBuffer( &indexBuffer );

		// Tangent stream
		if ( vertexBuffer.uiNumStreams > 1 && vertexBuffer.apVertexBuffers[ 1 ] )
			g_pRender->SetVertexBuffer( (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 1 ], sizeof( TVector4 ), 0, 1 );

		if ( bIsAnimated )
			UploadBonePalette( m_pBoneCBuffer, pSkeletonInstance, pSubMesh );

		// Draw mesh
		g_pRender->DrawIndexed(
		    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
		    pIndexPool->GetNumIndices(),
		    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
		    indexBuffer.uiIndexOffset,
		    DXGI_FORMAT_R16_UINT,
		    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
		    sizeof( TTMDWin::Vertex ),
		    vertexBuffer.uiVertexOffset,
			TNULL
		);
	}
}

void remaster::SkinShaderDX11::UploadDynamicLights( Toshi::TRenderPacket* a_pRenderPacket )
{
	g_pRender->GetLightManager().UploadDynamicLightsCBuffer( a_pRenderPacket );
}

void remaster::SkinShaderDX11::EnableRenderEnvMap( TBOOL a_bEnable )
{
}

TBOOL remaster::SkinShaderDX11::IsHighEndSkinning()
{
	return TTRUE;
}

void remaster::SkinShaderDX11::EnableHighEndSkinning( TBOOL a_bEnable )
{
}

TBOOL remaster::SkinShaderDX11::IsCapableHighEndSkinning()
{
	return TTRUE;
}

TBOOL remaster::SkinShaderDX11::IsLightScattering()
{
	return TFALSE;
}

void remaster::SkinShaderDX11::SetLightScattering( TBOOL a_bEnable )
{
}

TBOOL remaster::SkinShaderDX11::IsAlphaBlendMaterial()
{
	return TFALSE;
}

void remaster::SkinShaderDX11::SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial )
{
}

ASkinMaterial* remaster::SkinShaderDX11::CreateMaterial( const TCHAR* a_szName )
{
	TPROFILER_SCOPE();

	Validate();

	SkinMaterial* pMaterial = new SkinMaterial();
	pMaterial->SetShader( this );

	if ( TNULL != a_szName )
		pMaterial->SetName( a_szName );

	return pMaterial;
}

ASkinMesh* remaster::SkinShaderDX11::CreateMesh( const TCHAR* a_szName )
{
	TPROFILER_SCOPE();

	Validate();

	auto pMesh = new SkinMesh();
	pMesh->SetOwnerShader( this );

	return pMesh;
}

TINT remaster::SkinShaderDX11::AddLight( const Toshi::TVector3& a_rPosition, TFLOAT a_fIntensity )
{
	return -1;
}

void remaster::SkinShaderDX11::SetLight( TINT a_iIndex, const Toshi::TVector3& a_rPosition, TFLOAT a_fIntensity )
{
}

void remaster::SkinShaderDX11::RemoveLight( TINT a_iIndex )
{
}

TBOOL remaster::SkinShaderDX11::IsEnableRenderEnvMap()
{
	return TTRUE;
}

void remaster::SkinShaderDX11::SetSomeColour( TUINT a_uiR, TUINT a_uiG, TUINT a_uiB, TUINT a_uiA )
{
}

TINT remaster::SkinShaderDX11::SetUnknown1( TINT a_Unknown, TUINT8 a_fAlpha )
{
	return -1;
}

void remaster::SkinShaderDX11::SetUnknown2( TINT a_Unknown )
{
}
