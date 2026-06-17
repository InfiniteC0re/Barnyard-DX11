#include "pch.h"
#include "SkinShader.h"
#include "SkinMaterial.h"
#include "SkinMesh.h"
#include "WorldShader.h"
#include "Resource/ClassPatcher.h"

#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "RenderContentDX11.h"
#include "CSM/CSMManager.h"
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

#include "DynamicGlowLights.h"

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

	if ( g_bCSMEnabled && g_pCSMManager && g_flShadowIntensity > 0.0f )
	{
		g_pRender->PSSetShaderResource( 5, g_pCSMManager->GetShadowSRV() );
		g_pRender->PSSetSamplerState( 5, g_pCSMManager->GetShadowSampler() );
		g_pRender->PSSetConstantBuffer( 1, g_pRender->GetShadowConstantBuffer() );
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
	g_pRender->PSSetConstantBuffer( 2, TNULL );
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

	if ( !m_pBoneCBuffer )
	{
		D3D11_BUFFER_DESC boneDesc    = {};
		boneDesc.ByteWidth            = sizeof( TMatrix44 ) * MAX_SKIN_BONES;
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

const remaster::RenderDX11::ShaderPipelineState& remaster::SkinShaderDX11::GetSkinPipeline( TBOOL a_bBakedLighting, TBOOL a_bFOB, TBOOL a_bDynLighting, TBOOL a_bIsAnimated ) const
{
	TUINT uiComboFlags = 0;

	if ( a_bBakedLighting )
		uiComboFlags |= shadercombos::Skin_BAKED_LIGHTING;

	if ( a_bFOB )
		uiComboFlags |= shadercombos::Skin_FOB;

	if ( a_bIsAnimated )
		uiComboFlags |= shadercombos::Skin_ANIMATED;

	if ( !g_bCSMEnabled || !g_pCSMManager || g_flShadowIntensity <= 0.0f )
		uiComboFlags |= shadercombos::Skin_NO_CSM;

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	if ( !pCurrentContext->IsFogEnabled() || s_flFogDensity <= 0.0f )
		uiComboFlags |= shadercombos::Skin_NO_FOG;

	if ( !g_bDynamicGlowEnabled || !a_bDynLighting )
		uiComboFlags |= shadercombos::Skin_NO_DYN_LIGHT;

	return m_vecSkinPipelines[ shadercombos::GetSkinComboIndex( uiComboFlags ) ];
}

const remaster::RenderDX11::ShaderPipelineState& remaster::SkinShaderDX11::GetShadowPipeline( TBOOL a_bIsAnimated ) const
{
	TUINT uiComboFlags = 0;

	if ( a_bIsAnimated )
		uiComboFlags |= shadercombos::ShadowDepth_ANIMATED;

	uiComboFlags |= shadercombos::ShadowDepth_ALPHATEST;

	return m_vecSkinShadowPipelines[ shadercombos::GetShadowDepthComboIndex( uiComboFlags ) ];
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

		g_pRender->SetShaderPipelineState( GetShadowPipeline( bIsAnimated ) );

		// Upload MVP to GPU
		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			g_pRender->GetD3D11DeviceContext()->Map( pCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );

			TSTATICCAST(TMatrix44, mapped.pData )->Multiply( g_pCSMManager->GetCurrentLightProjection(), a_pRenderPacket->GetModelViewMatrix() );

			g_pRender->GetD3D11DeviceContext()->Unmap( pCBuffer, 0 );
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

			// Upload bones for this sub-mesh into the dedicated bone constant buffer.
			if ( bIsAnimated )
			{
				D3D11_MAPPED_SUBRESOURCE mapped;
				g_pRender->GetD3D11DeviceContext()->Map( m_pBoneCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
				TMatrix44* pBones = static_cast<TMatrix44*>( mapped.pData );
				for ( TUINT k = 0; k < pSubMesh->uiNumBones; k++ )
					pBones[ k ] = pSkeletonInstance->GetBone( pSubMesh->aBones[ k ] ).m_Transform;
				g_pRender->GetD3D11DeviceContext()->Unmap( m_pBoneCBuffer, 0 );
				
				g_pRender->VSSetConstantBuffer( 1, m_pBoneCBuffer );
			}

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
	const TBOOL  bIsFOB            = pMesh->IsFOB();
	const TBOOL  bHasDynLight      = g_bDynamicGlowEnabled && TINT8( a_pRenderPacket->m_ui8Unk1 ) >= 0;

	g_pRender->SetShaderPipelineState( GetSkinPipeline( bUseBakedLighting, bIsFOB, bHasDynLight, bIsAnimated ) );

	// Setup renderer
	if ( bUseBakedLighting )
	{
		g_pRender->PSSetShaderResource( 1, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_0 )->GetD3DTexture() ) );
		g_pRender->PSSetShaderResource( 2, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_1 )->GetD3DTexture() ) );
		g_pRender->PSSetShaderResource( 3, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_2 )->GetD3DTexture() ) );
		g_pRender->PSSetShaderResource( 4, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_3 )->GetD3DTexture() ) );
		g_pRender->PSSetSamplerState( 1, 5 );
	}

	TMatrix44 matWVP;
	matWVP.Multiply( pCurrentContext->GetProjectionMatrix(), a_pRenderPacket->GetModelViewMatrix() );

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

	// Fog settings
	TVector4 vMiscSettings;
	vMiscSettings.x = pCurrentContext->m_fFogDistanceStart;
	vMiscSettings.y = pCurrentContext->m_fFogDistanceEnd;

	TVector4 vFogColor = pCurrentContext->m_FogColor;
	vFogColor.w        = s_flFogDensity;

	// Upload data
	g_pRender->VSBufferSetMat4( 0, matWVP );
	g_pRender->VSBufferSetVec4( 4, vAmbientColor );
	g_pRender->VSBufferSetVec4( 5, vLightColour );
	g_pRender->VSBufferSetVec4( 6, vLightDirection );
	g_pRender->VSBufferSetVec4( 7, vUpAxis );
	g_pRender->VSBufferSetVec4( 8, vLightingLerp );
	g_pRender->VSBufferSetVec4( 9, vMiscSettings );
	g_pRender->VSBufferSetVec4( 10, vFogColor );
	g_pRender->VSBufferSetMat4( 11, matModel );

	if ( bHasDynLight ) UploadDynamicGlowLights( a_pRenderPacket );

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

		// Upload bones for this sub-mesh into the dedicated bone constant buffer.
		if ( bIsAnimated )
		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			g_pRender->GetD3D11DeviceContext()->Map( m_pBoneCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TMatrix44* pBones = static_cast<TMatrix44*>( mapped.pData );
			for ( TUINT k = 0; k < pSubMesh->uiNumBones; k++ )
				pBones[ k ] = pSkeletonInstance->GetBone( pSubMesh->aBones[ k ] ).m_Transform;
			g_pRender->GetD3D11DeviceContext()->Unmap( m_pBoneCBuffer, 0 );

			g_pRender->VSSetConstantBuffer( 1, m_pBoneCBuffer );
		}

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

void remaster::SkinShaderDX11::UploadDynamicGlowLights( Toshi::TRenderPacket* a_pRenderPacket )
{
	UploadDynamicGlowLightsCBuffer( a_pRenderPacket );
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

	/*if ( SkinShaderDX11::IsAlphaBlendMaterial() )
	{
		auto pAlphaBlendMaterial = new SkinMaterial();
		pAlphaBlendMaterial->SetShader( this );
		pAlphaBlendMaterial->Create( 1 );

		pMaterial->SetAlphaBlendMaterial( pAlphaBlendMaterial );
	}*/

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
