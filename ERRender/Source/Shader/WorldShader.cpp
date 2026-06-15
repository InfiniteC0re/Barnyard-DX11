#include "pch.h"
#include "WorldShader.h"
#include "WorldMaterial.h"
#include "WorldMesh.h"
#include "Generated/WorldShaderCombos.h"
#include "Generated/ShadowDepthShaderCombos.h"
#include "Resource/ClassPatcher.h"
#include "Ref/AWorld.h"

#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "RenderContentDX11.h"
#include "CSM/CSMManager.h"
#include "DynamicGlowLights.h"

#include <Render/TRenderPacket.h>
#include <Platform/DX8/TRenderInterface_DX8.h>
#include <Platform/DX8/TRenderContext_DX8.h>
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
	g_pRender->SetCullMode( TFALSE ? D3D11_CULL_BACK : D3D11_CULL_FRONT );
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
		g_pRender->SetCullMode( D3D11_CULL_FRONT );
		return;
	}

	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetBlendEnabled( TTRUE );
	g_pRender->SetCullMode( TFALSE ? D3D11_CULL_BACK : D3D11_CULL_FRONT );

	g_pRender->SetAlphaToCoverageEnabled( TTRUE );

	if ( g_bCSMEnabled && g_pCSMManager && g_flShadowIntensity > 0.0f )
	{
		g_pRender->PSSetShaderResource( 2, g_pCSMManager->GetShadowSRV() );
		g_pRender->PSSetSamplerState( 2, g_pCSMManager->GetShadowSampler() );
		g_pRender->PSSetConstantBuffer( 1, g_pRender->GetShadowConstantBuffer() );
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
	g_pRender->PSSetShaderResource( 6, TNULL );
	g_pRender->PSSetConstantBuffer( 2, TNULL );

	g_pRender->SetBlendEnabled( TFALSE );

	g_pRender->SetCullMode( D3D11_CULL_NONE );
	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetBlendEnabled( TFALSE );
}

void remaster::WorldShaderDX11::UploadDynamicGlowLights( Toshi::TRenderPacket* a_pRenderPacket )
{
	UploadDynamicGlowLightsCBuffer( a_pRenderPacket );
}

TBOOL remaster::WorldShaderDX11::Create()
{
	// Render with the same priority before everything to make the world serve as kind of depth prepass for rendering instances and skinned models
	m_aOrderTables[ 0 ].Create( this, -7000 );
	m_aOrderTables[ 1 ].Create( this, -7000 );
	m_aOrderTables[ 2 ].Create( this, -7000 );
	m_aOrderTables[ 3 ].Create( this, -7000 );
	m_aOrderTables[ 4 ].Create( this, -7000 );
	m_aOrderTables[ 5 ].Create( this, -7000 );
	m_aOrderTables[ 6 ].Create( this, -7000 );
	m_aOrderTables[ 7 ].Create( this, -7000 );

	m_oShadowTable.Create( this, 0 );

	m_oDummyMaterial.SetShader( this );
	m_oDummyMaterial.SetOrderTable( &m_oShadowTable, 0 );
	m_oDummyMaterial.CreateDummy();

	return BaseClass::Create();
}

TBOOL remaster::WorldShaderDX11::Validate()
{
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

	ID3D11InputLayout* pWorldInputLayout = TNULL;
	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aInputElements,
	        TARRAYSIZE( aInputElements ),
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

void remaster::WorldShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
	if ( !a_pRenderPacket || !a_pRenderPacket->GetMesh() ) return;

	TPROFILER_SCOPE();

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	AWorldMeshHAL*      pMesh           = TSTATICCAST( AWorldMeshHAL, a_pRenderPacket->GetMesh() );
	AWorldMaterialHAL*  pMaterial       = TSTATICCAST( AWorldMaterialHAL, pMesh->GetMaterial() );

	if ( g_pCSMManager && g_pCSMManager->IsRenderingShadowPass() )
	{
		g_pRender->SetShaderPipelineState( m_vecShadowDepthPipelines[ shadercombos::GetShadowDepthComboIndex( shadercombos::ShadowDepth_ALPHATEST ) ] );

		TMatrix44 mShadowMVP;
		mShadowMVP.Multiply( g_pCSMManager->GetCurrentLightProjection(), a_pRenderPacket->GetModelViewMatrix() );
		g_pRender->VSBufferSetMat4( 0, mShadowMVP );

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
		g_bHasGlowObjectsThisFrame = TTRUE;
		g_pRender->GetRenderTargetView( pOldRenderTargetView, pOldDepthStencilView );
		g_pRender->SetRenderTargetView( g_pRender->GetD3D11GlowRenderTargetView(), pOldDepthStencilView );
	}

	const TFLOAT flPacketAlpha = a_pRenderPacket->GetAlpha();
	const TBOOL  bIsBlending   = pMaterial->GetBlendMode() != 0 || flPacketAlpha < 1.0f || pMaterial->IsBlending();
	const TBOOL  bHasDynLight  = g_bDynamicGlowEnabled && TINT8( a_pRenderPacket->m_ui8Unk1 ) >= 0 && !bIsGlowing;
	g_pRender->SetBlendEnabled( bIsBlending );

	// Use either blending shader or alpharef shader
	// The only used alpharef value is 128, so no need to dynamically change it
	TUINT uiComboFlags = bIsBlending ? 0 : shadercombos::World_ALPHAREF;
	if ( bIsGlowing || pMesh->IsWater() || !g_bCSMEnabled || !g_pCSMManager || g_flShadowIntensity <= 0.0f )
		uiComboFlags |= shadercombos::World_NO_CSM;
	if ( bIsGlowing || !pCurrentContext->IsFogEnabled() || s_flFogDensity <= 0.0f )
		uiComboFlags |= shadercombos::World_NO_FOG;
	if ( !g_bDynamicGlowEnabled )
		uiComboFlags |= shadercombos::World_NO_DYN_LIGHT;
	if ( bIsGlowing )
		uiComboFlags |= shadercombos::World_GLOW;
	if ( !bHasDynLight )
		uiComboFlags |= shadercombos::World_NO_DYN_LIGHT;

	g_pRender->SetShaderPipelineState( m_vecWorldPipelines[ shadercombos::GetWorldComboIndex( uiComboFlags ) ] );

	// Fill vertex constant buffer
	// Setup model view projection matrix
	TMatrix44 mMVP;
	mMVP.Multiply( pCurrentContext->GetProjectionMatrix(), a_pRenderPacket->GetModelViewMatrix() );
	g_pRender->VSBufferSetMat4( 0, mMVP );

	TMatrix44 mModel;
	mModel.Multiply( pCurrentContext->GetViewWorldMatrix(), a_pRenderPacket->GetModelViewMatrix() );
	g_pRender->VSBufferSetMat4( 9, mModel );
	
	// Setup UV offset and alpha
	TVector4 vecUVOffsetAndAlpha;
	vecUVOffsetAndAlpha.x = pMaterial->GetUVOffsetX( 0 );
	vecUVOffsetAndAlpha.y = pMaterial->GetUVOffsetY( 0 );
	vecUVOffsetAndAlpha.z = flPacketAlpha;
	g_pRender->VSBufferSetVec4( 4, vecUVOffsetAndAlpha );

	// Setup colors
	g_pRender->VSBufferSetVec4( 5, m_AmbientColour );
	g_pRender->VSBufferSetVec4( 6, m_ShadowColour );

	// Setup material settings
	TVector4 vMiscSettings;
	vMiscSettings.x = 0.0f; // isWater
	vMiscSettings.y = 1.0f; // isLit

	if ( pMesh->IsWater() )
	{
		vMiscSettings.x = 1.0f; // isWater
		vMiscSettings.y = 0.0f; // isLit
	}

	// Fog settings
	vMiscSettings.z = pCurrentContext->m_fFogDistanceStart;
	vMiscSettings.w = pCurrentContext->m_fFogDistanceEnd;

	TVector4 vFogColor = pCurrentContext->m_FogColor;
	vFogColor.w        = s_flFogDensity;

	g_pRender->VSBufferSetVec4( 7, vMiscSettings );
	g_pRender->VSBufferSetVec4( 8, vFogColor );
	if ( bHasDynLight ) UploadDynamicGlowLights( a_pRenderPacket );

	// Set vertices
	TVertexPoolResource* pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
	TIndexPoolResource*  pIndexPool  = TSTATICCAST( TIndexPoolResource, pMesh->GetSubMesh( 0 )->pIndexPool );
	TVALIDPTR( pVertexPool );
	TVALIDPTR( pIndexPool );

	TVertexBlockResource::HALBuffer vertexBuffer;
	CALL_THIS( 0x006d6660, TVertexPoolResource*, TBOOL, pVertexPool, TVertexBlockResource::HALBuffer&, vertexBuffer ); // pVertexPool->GetHALBuffer( &vertexBuffer );

	TIndexBlockResource::HALBuffer indexBuffer;
	CALL_THIS( 0x006d6180, TIndexPoolResource*, TBOOL, pIndexPool, TIndexBlockResource::HALBuffer&, indexBuffer ); // pIndexPool->GetHALBuffer( &indexBuffer );

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
		g_pRender->SetRenderTargetView( pOldRenderTargetView, pOldDepthStencilView );
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

	/*if ( WorldMaterial::IsAlphaBlendMaterial() )
	{
		auto pAlphaBlendMaterial = new AWorldMaterialHAL();
		pAlphaBlendMaterial->SetShader( this );
		pAlphaBlendMaterial->Create( 1 );

		pMaterial->SetAlphaBlendMaterial( pAlphaBlendMaterial );
	}*/

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
