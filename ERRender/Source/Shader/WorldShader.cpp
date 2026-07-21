#include "pch.h"
#include "WorldShader.h"
#include "GameSettings.h"
#include "MaterialParams.h"
#include "WorldMaterial.h"
#include "WorldMesh.h"
#include "Generated/WorldShaderCombos.h"
#include "Generated/ShadowDepthShaderCombos.h"
#include "Resource/ClassPatcher.h"
#include "Resource/TextureResource.h"
#include "Ref/AWorld.h"

#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "RenderContentDX11.h"
#include "CSM/CSMManager.h"
#include "LightManager.h"
#include "CubemapAnchors.h"
#include "SkyCube.h"
#include "RenderParams.h"

#include <Render/TRenderPacket.h>
#include <Platform/DX8/TRenderInterface_DX8.h>
#include <Platform/DX8/TRenderContext_DX8.h>
#include <Platform/DX8/TTextureResourceHAL_DX8.h>
#include <Platform/DX8/TVertexBlockResource_DX8.h>
#include <Platform/DX8/TVertexPoolResource_DX8.h>
#include <Platform/DX8/TIndexBlockResource_DX8.h>
#include <Platform/DX8/TIndexPoolResource_DX8.h>

#include <AHooks.h>
#include <HookHelpers.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::WorldShaderDX11, 0x0079a980 );

MEMBER_HOOK( 0x005f75e0, remaster::WorldShaderDX11, AWorldShaderHAL_Constructor, remaster::WorldShaderDX11* )
{
	TFree( this );
	return new remaster::WorldShaderDX11();
}

MEMBER_HOOK( 0x005f69e0, remaster::WorldMaterial, AWorldMaterialHAL_SetOrderTable, void, Toshi::TOrderTable* a_pOrderTable, TINT a_iUnused )
{
	SetOrderTable( a_pOrderTable, a_iUnused );
}

void remaster::SetupRenderHooks_WorldShader()
{
	InstallHook<AWorldShaderHAL_Constructor>();
	InstallHook<AWorldMaterialHAL_SetOrderTable>();
}

remaster::WorldShaderDX11::WorldShaderDX11()
{
	// Set Singleton
	*(AWorldShader**)( 0x0079a854 ) = this;
}

remaster::WorldShaderDX11::~WorldShaderDX11()
{
}

void remaster::WorldShaderDX11::Flush()
{
	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetBlendEnabled( TTRUE );
	g_pRender->SetCullMode( D3D11_CULL_NONE );
	g_pRender->SetAlphaToCoverageEnabled( TTRUE );
}

static TFLOAT s_flFogDensity = 0.0f;

void remaster::WorldShaderDX11::StartFlush()
{
	if ( !IsValidated() ) return;

	if ( g_pCSMManager && g_pCSMManager->IsRenderingShadowPass() )
	{
		g_pRender->SetDepthEnabled( TTRUE );
		g_pRender->SetDepthWrite( TTRUE );
		g_pRender->SetBlendEnabled( TFALSE );
		g_pRender->SetAlphaToCoverageEnabled( TFALSE );
		g_pRender->SetCullMode( D3D11_CULL_NONE );
		return;
	}

	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetBlendEnabled( TTRUE );
	g_pRender->SetCullMode( D3D11_CULL_NONE );

	g_pRender->SetAlphaToCoverageEnabled( TTRUE );

	if ( g_bInMainScenePass && GameSettings::IsCSMEnabled() && g_pCSMManager && g_flShadowIntensity > 0.0f )
	{
		g_pRender->PSSetShaderResource( 2, g_pCSMManager->GetShadowSRV() );
		g_pRender->PSSetSamplerState( 2, g_pCSMManager->GetShadowSampler() );
		g_pRender->PSSetConstantBuffer( 1, g_pRender->GetShadowConstantBuffer() );

		// Animated cloud shadow map (t9/s3), sampled by world XZ in SampleShadow.
		if ( GameSettings::AreCloudShadowsEnabled() )
		{
			g_pRender->PSSetShaderResource( 9, g_pCloudShadowSRV );
			g_pRender->PSSetSamplerState( 3, g_pCloudShadowSampler );
		}
	}

	// bind the reflection cube (last frame) + linear-clamp sampler for env specular; skipped during
	// the capture (the cube is an RTV then -- RTV/SRV conflict, and no recursive reflections)
	if ( !g_bReflectionCaptureActive && g_oSkyCubeBlend.pSRVTo )
	{
		g_pRender->PSSetShaderResource( 5, g_oSkyCubeBlend.pSRVTo );   // active ("to") cube
		g_pRender->PSSetShaderResource( 7, g_oSkyCubeBlend.pSRVFrom ); // outgoing ("from") cube (t6 = glow shadows)
		g_pRender->PSSetSamplerState( 1, SAMPLER_LINEAR_CLAMP );
	}

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	s_flFogDensity                      = dx11::CalculateFogDensity(
        pCurrentContext->m_fFogDistanceStart,
        pCurrentContext->m_fFogDistanceEnd
    );
}

void remaster::WorldShaderDX11::EndFlush()
{
	if ( g_pCSMManager && g_pCSMManager->IsRenderingShadowPass() )
		return;

	g_pRender->PSSetShaderResource( 0, TNULL );
	g_pRender->PSSetShaderResource( 1, TNULL );
	g_pRender->PSSetShaderResource( 2, TNULL );
	g_pRender->PSSetShaderResource( 5, TNULL );
	g_pRender->PSSetShaderResource( 6, TNULL );
	g_pRender->PSSetShaderResource( 7, TNULL );
	g_pRender->PSSetShaderResource( 9, TNULL );
	g_pRender->PSSetConstantBuffer( 2, TNULL );

	g_pRender->SetBlendEnabled( TFALSE );

	g_pRender->SetCullMode( D3D11_CULL_NONE );
	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetBlendEnabled( TFALSE );
}

void remaster::WorldShaderDX11::UploadDynamicLights( Toshi::TRenderPacket* a_pRenderPacket )
{
	g_pRender->GetLightManager().UploadDynamicLightsCBuffer( a_pRenderPacket );
}

TBOOL remaster::WorldShaderDX11::Create()
{
	m_aOrderTables[ 0 ].Create( this, -3000 );
	m_aOrderTables[ 1 ].Create( this, 100 );
	m_aOrderTables[ 2 ].Create( this, 101 );
	m_aOrderTables[ 3 ].Create( this, 601 );
	m_aOrderTables[ 4 ].Create( this, -400 );
	m_aOrderTables[ 5 ].Create( this, 500 );
	m_aOrderTables[ 6 ].Create( this, -6005 );
	m_aOrderTables[ 7 ].Create( this, -7000 );

	m_oShadowTable.Create( this, 0 );

	m_oDummyMaterial.SetShader( this );
	m_oDummyMaterial.SetOrderTable( &m_oShadowTable, 0 );
	m_oDummyMaterial.CreateDummy();

	return BaseClass::Create();
}

TBOOL remaster::WorldShaderDX11::Validate()
{
	if ( !ShaderWarmup_IsComplete() )
		return TFALSE;

	if ( IsValidated() )
		return TTRUE;

	dx11::ShaderCombo& rWorldVSCombo       = shadercombos::GetWorldVertexShaderCombo_vs_main();
	dx11::ShaderCombo& rWorldPSCombo       = shadercombos::GetWorldPixelShaderCombo_ps_main();
	dx11::ShaderCombo& rShadowDepthVSCombo = shadercombos::GetShadowDepthVertexShaderCombo_vs_main_world();
	dx11::ShaderCombo& rShadowDepthPSCombo = shadercombos::GetShadowDepthPixelShaderCombo_ps_main();

	D3D11_INPUT_ELEMENT_DESC aInputElements[] = {
		{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "NORMAL", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "COLOR", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "TEXCOORD", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
	};

	// World layout adds the per-vertex tangent stream on slot 1. The shadow layout keeps
	// the base attributes only since the shadow VS doesn't read tangents.
	D3D11_INPUT_ELEMENT_DESC aWorldInputElements[] = {
		aInputElements[ 0 ],
		aInputElements[ 1 ],
		aInputElements[ 2 ],
		aInputElements[ 3 ],
		{ .SemanticName = "TANGENT", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .InputSlot = 1, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
	};

	ID3D11InputLayout* pWorldInputLayout = TNULL;
	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aWorldInputElements,
	        TARRAYSIZE( aWorldInputElements ),
	        rWorldVSCombo.GetBlob( 0 )->GetBufferPointer(),
	        rWorldVSCombo.GetBlob( 0 )->GetBufferSize(),
	        &pWorldInputLayout
	    )
	);

	ID3D11InputLayout* pShadowInputLayout = TNULL;
	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aInputElements,
	        TARRAYSIZE( aInputElements ),
	        rShadowDepthVSCombo.GetBlob( 0 )->GetBufferPointer(),
	        rShadowDepthVSCombo.GetBlob( 0 )->GetBufferSize(),
	        &pShadowInputLayout
	    )
	);

	TASSERT( shadercombos::CreateWorldShaderPipelines( rWorldVSCombo, &rWorldPSCombo, pWorldInputLayout, m_vecWorldPipelines, "World" ) );
	TASSERT( shadercombos::CreateShadowDepthShaderPipelines( rShadowDepthVSCombo, &rShadowDepthPSCombo, pShadowInputLayout, m_vecShadowDepthPipelines, "World_Shadow" ) );

	// HACK: Disable pixel shader manually, because generator doesn't
	const TUINT uiAlphaTestBit = shadercombos::GetShadowDepthComboIndex( shadercombos::ShadowDepth_ALPHATEST );
	for ( TINT i = 0; i < m_vecShadowDepthPipelines.Size(); i++ )
	{
		if ( !( TUINT( i ) & uiAlphaTestBit ) )
			m_vecShadowDepthPipelines[ i ].ppPixelShader = TNULL;
	}

	return BaseClass::Validate();
}

void remaster::WorldShaderDX11::Invalidate()
{
}

TBOOL remaster::WorldShaderDX11::TryInvalidate()
{
	return TFALSE;
}

TBOOL remaster::WorldShaderDX11::TryValidate()
{
	return TFALSE;
}

extern TBOOL g_bHasGlowObjectsThisFrame;
extern TBOOL g_bEnableWaterReflections;

void remaster::WorldShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
	if ( !a_pRenderPacket || !a_pRenderPacket->GetMesh() ) return;

	TPROFILER_SCOPE();

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	AWorldMeshHAL*      pMesh           = TSTATICCAST( AWorldMeshHAL, a_pRenderPacket->GetMesh() );
	WorldMaterial*      pMaterial       = TSTATICCAST( WorldMaterial, pMesh->GetMaterial() );

	if ( g_pCSMManager && g_pCSMManager->IsRenderingShadowPass() )
	{
		// Match the main pass's wind so the shadow silhouette sways with the geometry
		const remaster::MaterialParams* pMatParams = pMaterial->GetMaterialParams();
		const TBOOL bShadowWind = GameSettings::IsWindEnabled() && pMatParams && pMatParams->bWind;

		const TBOOL bShadowAlphaTest = ( ( pMatParams && pMatParams->iShadowAlphaTest >= 0 )
		    ? pMatParams->iShadowAlphaTest != 0
		    : remaster::TextureResource_HasTransparency( pMaterial->GetTexture( 0 ) ) ) && ( !pMatParams || !pMatParams->bFOB || g_pCSMManager->GetCurrentCascade() <= 1 );

		TUINT uiShadowFlags = bShadowAlphaTest ? shadercombos::ShadowDepth_ALPHATEST : 0;
		if ( bShadowWind )
			uiShadowFlags |= shadercombos::ShadowDepth_WIND;
		g_pRender->SetShaderPipelineState( m_vecShadowDepthPipelines[ shadercombos::GetShadowDepthComboIndex( uiShadowFlags ) ] );

		TMatrix44 mShadowMVP;
		mShadowMVP.Multiply( g_pCSMManager->GetCurrentLightProjection(), a_pRenderPacket->GetModelViewMatrix() );
		g_pRender->VSBufferSetMat4( 0, mShadowMVP );
		g_pRender->VSBufferSetVec4( 4, TVector4( TFLOAT( g_pCSMManager->GetCurrentCascade() ), 0.0f, 0.0f ) );

		// Wind params (slots 5/6) mirror the main-pass upload so the deformation is identical
		if ( bShadowWind )
		{
			g_pRender->VSBufferSetVec4( 5, TVector4( g_flWindDir[ 0 ], g_flWindDir[ 1 ], g_flWindStrength, g_flWindTime ) );
			g_pRender->VSBufferSetVec4( 6, TVector4( pMatParams->fWindMin, pMatParams->fWindMax, 0.0f, 0.0f ) );
		}

		TVertexPoolResource* pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
		TIndexPoolResource*  pIndexPool  = TSTATICCAST( TIndexPoolResource, pMesh->GetSubMesh( 0 )->pIndexPool );
		TVALIDPTR( pVertexPool );
		TVALIDPTR( pIndexPool );

		TVertexBlockResource::HALBuffer vertexBuffer;
		CALL_THIS( 0x006d6660, TVertexPoolResource*, TBOOL, pVertexPool, TVertexBlockResource::HALBuffer&, vertexBuffer ); // pVertexPool->GetHALBuffer( &vertexBuffer );

		TIndexBlockResource::HALBuffer indexBuffer;
		CALL_THIS( 0x006d6180, TIndexPoolResource*, TBOOL, pIndexPool, TIndexBlockResource::HALBuffer&, indexBuffer ); // pIndexPool->GetHALBuffer( &indexBuffer );

		g_pRender->DrawIndexed(
		    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
		    pIndexPool->GetNumIndices(),
		    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
		    indexBuffer.uiIndexOffset,
		    DXGI_FORMAT_R16_UINT,
		    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
		    sizeof( WorldVertex ),
		    vertexBuffer.uiVertexOffset,
		    TNULL
		);
		return;
	}

	// Normal pass
	const TBOOL bIsGlowing = pMaterial->GetFlags() & TMaterial::FLAGS_GLOW;

	ID3D11RenderTargetView* pOldRenderTargetView;
	ID3D11DepthStencilView* pOldDepthStencilView;
	if ( bIsGlowing )
	{
		if ( !remaster::g_bReflectionCaptureActive )
		{
			g_bHasGlowObjectsThisFrame = TTRUE;
			g_pRender->GetRenderTargetView( pOldRenderTargetView, pOldDepthStencilView );
			g_pRender->SetRenderTargetView( g_pRender->GetD3D11GlowRenderTargetView(), pOldDepthStencilView );
		}

		g_pRender->SetDepthBias( -10 );
	}

	const TFLOAT flPacketAlpha = a_pRenderPacket->GetAlpha();
	const TBOOL  bIsBlending   = pMaterial->GetBlendMode() != 0 || flPacketAlpha < 1.0f || pMaterial->IsBlending();
	const TBOOL  bHasDynLight  = GameSettings::AreDynamicLightsEnabled() && RenderPacketHasDynamicLights( a_pRenderPacket ) && !bIsGlowing;
	g_pRender->SetBlendEnabled( bIsBlending );

	const remaster::MaterialParams* pMatParams = pMaterial->GetMaterialParams();

	// Use either blending shader or alpharef shader
	// The only used alpharef value is 128, so no need to dynamically change it
	TUINT uiComboFlags = 0;
	if ( !bIsBlending && !remaster::TextureResource_IsOpaque( pMaterial->GetTexture( 0 ) ) )
		uiComboFlags |= shadercombos::World_ALPHAREF;
	if ( !g_bInMainScenePass || bIsGlowing || pMesh->IsWater() || !GameSettings::IsCSMEnabled() || !g_pCSMManager || g_flShadowIntensity <= 0.0f )
		uiComboFlags |= shadercombos::World_NO_CSM;
	if ( bIsGlowing || !pCurrentContext->IsFogEnabled() || s_flFogDensity <= 0.0f )
		uiComboFlags |= shadercombos::World_NO_FOG;
	if ( !GameSettings::AreDynamicLightsEnabled() )
		uiComboFlags |= shadercombos::World_NO_DYN_LIGHT;
	if ( bIsGlowing )
		uiComboFlags |= shadercombos::World_GLOW;
	if ( !bHasDynLight )
		uiComboFlags |= shadercombos::World_NO_DYN_LIGHT;
	// Per-material map sampling and POM compile out for meshes that don't use them.
	if ( pMatParams && ( pMatParams->pNormalMap || pMatParams->pRoughnessMap || pMatParams->pMetallicMap ) )
		uiComboFlags |= shadercombos::World_MATERIAL_MAPS;
	if ( pMatParams && pMatParams->pHeightMap )
		uiComboFlags |= shadercombos::World_PARALLAX;
	if ( GameSettings::AreCloudShadowsEnabled() )
		uiComboFlags |= shadercombos::World_CLOUD_SHADOWS;
	// Wind vertex deformation (blue vertex-color channel = strength); opt-in via the XML "wind"
	// flag + global master toggle. Glow meshes opt out so their geometry stays put
	const TBOOL bWind = GameSettings::IsWindEnabled() && !bIsGlowing && pMatParams && pMatParams->bWind;
	if ( bWind )
		uiComboFlags |= shadercombos::World_WIND;

	// Wii-style FOB tree billboards, opt-in via the XML "fob" flag; the per-instance selector
	// arrives through light-colour row 0 (see WorldMesh)
	const TBOOL bFOB = pMatParams && pMatParams->bFOB;
	if ( bFOB )
		uiComboFlags |= shadercombos::World_FOB;

	g_pRender->SetShaderPipelineState( m_vecWorldPipelines[ shadercombos::GetWorldComboIndex( uiComboFlags ) ] );

	// Only the model matrix ships per draw; the clip transform (world * pp_matViewProj) is refreshed at pass boundaries
	g_pRender->UpdatePassViewProj( pCurrentContext->GetProjectionMatrix(), pCurrentContext->GetWorldViewMatrix() );

	TMatrix44 mModel;
	mModel.Multiply( pCurrentContext->GetViewWorldMatrix(), a_pRenderPacket->GetModelViewMatrix() );
	g_pRender->VSBufferSetMat4( 0, mModel );

	// Setup UV offset and alpha
	TVector4 vecUVOffsetAndAlpha;
	vecUVOffsetAndAlpha.x = pMaterial->GetUVOffsetX( 0 );
	vecUVOffsetAndAlpha.y = pMaterial->GetUVOffsetY( 0 );
	vecUVOffsetAndAlpha.z = flPacketAlpha;
	g_pRender->VSBufferSetVec4( 4, vecUVOffsetAndAlpha );

	// Setup colors
	if ( bFOB )
	{
		// Reproduce the Wii FOB TEV colours: lit colour = per-tree-type tint * shadow->sun blend at
		// the tree's baked sun exposure; the vertex colour then blends shadow->lit in the VS. Tints
		// match the Wii ATreeManager table (s_vecUnused1-3 in the decomp); type 3 is untinted
		static const TVector4 s_aTreeTints[ 4 ] = {
			TVector4( 0.7372549f, 0.8156863f, 0.5254902f, 1.0f ),
			TVector4( 0.54509807f, 0.60784316f, 0.47058824f, 1.0f ),
			TVector4( 0.9529412f, 0.75686276f, 0.54509807f, 1.0f ),
			TVector4( 1.0f, 1.0f, 1.0f, 1.0f ),
		};

		// No valid selector (not from ATreeManager2) -> fully lit, untinted
		const Toshi::TVector3& vFOBSelector   = a_pRenderPacket->GetLightColour();
		const TBOOL            bValidSelector = vFOBSelector.z < -0.5f;

		TFLOAT flExposure = bValidSelector ? vFOBSelector.x : 1.0f;
		TMath::Clip( flExposure, 0.0f, 1.0f );
		const TVector4& vecTint = s_aTreeTints[ bValidSelector ? ( TINT( vFOBSelector.y ) & 3 ) : 3 ];

		TVector4 vecLitColour;
		vecLitColour.x = ( m_ShadowColour.x + ( m_AmbientColour.x - m_ShadowColour.x ) * flExposure ) * vecTint.x;
		vecLitColour.y = ( m_ShadowColour.y + ( m_AmbientColour.y - m_ShadowColour.y ) * flExposure ) * vecTint.y;
		vecLitColour.z = ( m_ShadowColour.z + ( m_AmbientColour.z - m_ShadowColour.z ) * flExposure ) * vecTint.z;
		vecLitColour.w = 1.0f;

		g_pRender->VSBufferSetVec4( 5, vecLitColour );
	}

	// Ambient/shadow colours are per-pass (b4); the FOB combo reads its per-draw lit colour from slot 5 instead
	g_pRender->PassBufferSetVec4( PASSBUF_AMBIENT_COLOR, m_AmbientColour );
	g_pRender->PassBufferSetVec4( PASSBUF_SHADOW_COLOR, m_ShadowColour );

	if ( pMesh->IsWater() && g_bEnableWaterReflections )
		g_pRender->SetBlendEnabled( TFALSE );

	TVector4 vFogColor = pCurrentContext->m_FogColor;
	vFogColor.w        = s_flFogDensity;
	g_pRender->PassBufferSetVec4( PASSBUF_FOG_PARAMS, TVector4( pCurrentContext->m_fFogDistanceStart, pCurrentContext->m_fFogDistanceEnd, 0.0f, 0.0f ) );
	g_pRender->PassBufferSetVec4( PASSBUF_FOG_COLOR, vFogColor );

	const TVector3 sunDir = g_pCSMManager ? g_pCSMManager->GetLightDirection() : TVector3( 0.0f, -1.0f, 0.0f );
	g_pRender->PassBufferSetVec4( PASSBUF_SUN_DIRECTION, TVector4( -sunDir.x, sunDir.y, -sunDir.z, 0.0f ) );
	const TVector3 camPos = pCurrentContext->GetViewWorldMatrix().GetTranslation3();
	g_pRender->PassBufferSetVec4( PASSBUF_CAMERA_POS, TVector4( camPos.x, camPos.y, camPos.z, 0.0f ) );

	// Per-material normal/roughness/height/metallic map SRVs; presence flags live in the material
	// record (b5), maps reuse the albedo sampler at s0
	if ( pMatParams && pMatParams->pNormalMap )
		g_pRender->PSSetShaderResource( 1, (ID3D11ShaderResourceView*)pMatParams->pNormalMap );
	if ( pMatParams && pMatParams->pRoughnessMap )
		g_pRender->PSSetShaderResource( 3, (ID3D11ShaderResourceView*)pMatParams->pRoughnessMap );
	if ( pMatParams && pMatParams->pMetallicMap )
		g_pRender->PSSetShaderResource( 8, (ID3D11ShaderResourceView*)pMatParams->pMetallicMap );
	if ( pMatParams && pMatParams->pHeightMap )
		g_pRender->PSSetShaderResource( 4, (ID3D11ShaderResourceView*)pMatParams->pHeightMap );

	// Misc params (slot 6): x = isWater, y = isLit
	const TBOOL bIsWater = pMesh->IsWater();
	g_pRender->VSBufferSetVec4( 6, TVector4( bIsWater ? 1.0f : 0.0f, bIsWater ? 0.0f : 1.0f, 0.0f, 0.0f ) );

	// All static material values come from the immutable material buffer (b5)
	remaster::BindMaterialConstants( pMatParams, remaster::MATBUF_DEFAULT_WORLD );

	// Per-pass env-specular vec4: [intensity (0 while capturing/off), cube max mip (roughness->LOD),
	// capture-active mask (kills metallic/parallax so metals' darkened diffuse doesn't bake into the
	// cube they'll later reflect), tangent-debug]
	const TFLOAT flEnvIntensity = ( g_bReflectionCaptureActive || !GameSettings::IsEnvSpecularEnabled() ) ? 0.0f : 1.0f;
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_SPECULAR,
	    TVector4( flEnvIntensity, TFLOAT( g_iSkyCubeMaxMip ), g_bReflectionCaptureActive ? 1.0f : 0.0f, remaster::g_bDebugTangents ? 1.0f : 0.0f ) );

	// Env-specular cross-fade (per-pass): "to" cube (box+probe, w=blend) + outgoing "from" cube,
	// from the capture blend state so each probe centre matches where its cube was rendered
	// (mismatch makes the reflection swim). Steady state: blend = 1
	const remaster::SkyCubeBlendState& rBlend = remaster::g_oSkyCubeBlend;
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_PARALLAX, TVector4( rBlend.vBoxTo.x, rBlend.vBoxTo.y, rBlend.vBoxTo.z, 0.0f ) );
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_PROBE_POS, TVector4( rBlend.vProbeTo.x, rBlend.vProbeTo.y, rBlend.vProbeTo.z, rBlend.flBlend ) );
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_PARALLAX2, TVector4( rBlend.vBoxFrom.x, rBlend.vBoxFrom.y, rBlend.vBoxFrom.z, 0.0f ) );
	g_pRender->PassBufferSetVec4( PASSBUF_ENV_PROBE_POS2, TVector4( rBlend.vProbeFrom.x, rBlend.vProbeFrom.y, rBlend.vProbeFrom.z, 0.0f ) );

	// Wind: direction/strength/time are per-pass; [windMin,windMax] remap rides in the material record
	g_pRender->PassBufferSetVec4( PASSBUF_WIND_PARAMS, TVector4( g_flWindDir[ 0 ], g_flWindDir[ 1 ], g_flWindStrength, g_flWindTime ) );

	if ( bHasDynLight ) UploadDynamicLights( a_pRenderPacket );

	// Always upload so the shader's static-light count is never stale. Glow meshes receive no
	// light, so pass null for a zero count (dynamic is already gated off for them above)
	g_pRender->GetLightManager().UploadCellStaticLightIndices( bIsGlowing ? TNULL : a_pRenderPacket, 7, 9 );

	// Set vertices
	TVertexPoolResource* pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
	TIndexPoolResource*  pIndexPool  = TSTATICCAST( TIndexPoolResource, pMesh->GetSubMesh( 0 )->pIndexPool );
	TVALIDPTR( pVertexPool );
	TVALIDPTR( pIndexPool );

	TVertexBlockResource::HALBuffer vertexBuffer;
	CALL_THIS( 0x006d6660, TVertexPoolResource*, TBOOL, pVertexPool, TVertexBlockResource::HALBuffer&, vertexBuffer ); // pVertexPool->GetHALBuffer( &vertexBuffer );

	TIndexBlockResource::HALBuffer indexBuffer;
	CALL_THIS( 0x006d6180, TIndexPoolResource*, TBOOL, pIndexPool, TIndexBlockResource::HALBuffer&, indexBuffer ); // pIndexPool->GetHALBuffer( &indexBuffer );

	// Bind the per-vertex tangent stream on slot 1. DrawIndexed applies the per-mesh
	// vertex offset via BaseVertexLocation, so both streams share it at byte 0.
	if ( vertexBuffer.uiNumStreams > 1 && vertexBuffer.apVertexBuffers[ 1 ] )
		g_pRender->SetVertexBuffer( (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 1 ], sizeof( TVector4 ), 0, 1 );

	// Draw mesh
	g_pRender->DrawIndexed(
	    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	    pIndexPool->GetNumIndices(),
	    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
	    indexBuffer.uiIndexOffset,
	    DXGI_FORMAT_R16_UINT,
	    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
	    sizeof( WorldVertex ),
	    vertexBuffer.uiVertexOffset,
	    TNULL
	);

	// Restore usual render target if needed
	if ( bIsGlowing )
	{
		if ( !remaster::g_bReflectionCaptureActive ) g_pRender->SetRenderTargetView( pOldRenderTargetView, pOldDepthStencilView );
		g_pRender->SetDepthBias( 0 );
	}
}

void remaster::WorldShaderDX11::EnableRenderEnvMap( TBOOL a_bEnable )
{
}

TBOOL remaster::WorldShaderDX11::IsAlphaBlendMaterial()
{
	return TFALSE;
}

void remaster::WorldShaderDX11::SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial )
{
}

AWorldMaterial* remaster::WorldShaderDX11::CreateMaterial( const TCHAR* a_szName )
{
	TPROFILER_SCOPE();

	Validate();

	auto pMaterial = new WorldMaterial();
	pMaterial->SetShader( this );

	return pMaterial;
}

AWorldMesh* remaster::WorldShaderDX11::CreateMesh( const TCHAR* a_szName )
{
	TPROFILER_SCOPE();

	Validate();

	auto pMesh = new WorldMesh();
	pMesh->SetOwnerShader( this );

	return pMesh;
}

TBOOL remaster::WorldShaderDX11::IsHighEndMode()
{
	return TTRUE;
}

void remaster::WorldShaderDX11::SetHighEndMode( TBOOL a_bEnable )
{
}

TBOOL remaster::WorldShaderDX11::IsCapableShaders()
{
	return TTRUE;
}

TBOOL remaster::WorldShaderDX11::IsRenderEnvMapEnabled()
{
	return TTRUE;
}

void* remaster::WorldShaderDX11::CreateUnknown( void*, void*, void*, void* )
{
	return TNULL;
}
