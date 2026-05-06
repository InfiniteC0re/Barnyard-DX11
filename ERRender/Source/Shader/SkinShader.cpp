#include "pch.h"
#include "SkinShader.h"
#include "SkinMaterial.h"
#include "SkinMesh.h"
#include "WorldShader.h"
#include "Resource/ClassPatcher.h"

#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "RenderContentDX11.h"

#include <Render/TTMDWin.h>
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
{
	// Set Singleton
	*(ASkinShader**)( 0x0079a4f8 ) = this;
}

remaster::SkinShaderDX11::~SkinShaderDX11()
{
}

void remaster::SkinShaderDX11::Flush()
{
	if ( IsValidated() )
	{
		g_pRender->SetBlendEnabled( TTRUE );

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

	g_pRender->SetBlendEnabled( TTRUE );
	g_pRender->SetCullMode( m_bRenderEnvMap ? D3D11_CULL_BACK : D3D11_CULL_FRONT );
	g_pRender->SetAlphaToCoverageEnabled( TTRUE );

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	s_flFogDensity                      = dx11::CalculateFogDensity(
        pCurrentContext->m_fFogDistanceStart,
        pCurrentContext->m_fFogDistanceEnd
    );
}

void remaster::SkinShaderDX11::EndFlush()
{
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

	D3D_SHADER_MACRO aBakedLightingShaderMacro[] = { "BAKED_LIGHTING", "1", TNULL, TNULL };
	D3D_SHADER_MACRO aFOBShaderMacro[] = { "FOB", "1", TNULL, TNULL };

	m_pVSShaderBlob_RuntimeLighting     = dx11::CompileShaderFromFile( "Data\\Shaders\\Skin.hlsl", "vs_main", "vs_5_0", TNULL );
	m_pVSShaderBlob_RuntimeLighting_FOB = dx11::CompileShaderFromFile( "Data\\Shaders\\Skin.hlsl", "vs_main", "vs_5_0", aFOBShaderMacro );
	m_pPSShaderBlob_RuntimeLighting     = dx11::CompileShaderFromFile( "Data\\Shaders\\Skin.hlsl", "ps_main", "ps_5_0", TNULL );
	m_pVSShaderBlob_BakedLighting       = dx11::CompileShaderFromFile( "Data\\Shaders\\Skin.hlsl", "vs_main", "vs_5_0", aBakedLightingShaderMacro );
	m_pPSShaderBlob_BakedLighting       = dx11::CompileShaderFromFile( "Data\\Shaders\\Skin.hlsl", "ps_main", "ps_5_0", aBakedLightingShaderMacro );

	TASSERT( m_pVSShaderBlob_BakedLighting && m_pPSShaderBlob_BakedLighting );
	DX11_API_VALIDATE( dx11::CreateVertexShader( m_pVSShaderBlob_BakedLighting->GetBufferPointer(), m_pVSShaderBlob_BakedLighting->GetBufferSize(), &m_oShaderPipeline_BakedLighting.pVertexShader ) );
	DX11_API_VALIDATE( dx11::CreatePixelShader( m_pPSShaderBlob_BakedLighting->GetBufferPointer(), m_pPSShaderBlob_BakedLighting->GetBufferSize(), &m_oShaderPipeline_BakedLighting.pPixelShader ) );

	TASSERT( m_pVSShaderBlob_RuntimeLighting && m_pPSShaderBlob_RuntimeLighting );
	DX11_API_VALIDATE( dx11::CreateVertexShader( m_pVSShaderBlob_RuntimeLighting->GetBufferPointer(), m_pVSShaderBlob_RuntimeLighting->GetBufferSize(), &m_oShaderPipeline_RuntimeLighting.pVertexShader ) );
	DX11_API_VALIDATE( dx11::CreatePixelShader( m_pPSShaderBlob_RuntimeLighting->GetBufferPointer(), m_pPSShaderBlob_RuntimeLighting->GetBufferSize(), &m_oShaderPipeline_RuntimeLighting.pPixelShader ) );

	TASSERT(m_pVSShaderBlob_RuntimeLighting_FOB );
	DX11_API_VALIDATE( dx11::CreateVertexShader( m_pVSShaderBlob_RuntimeLighting_FOB->GetBufferPointer(), m_pVSShaderBlob_RuntimeLighting_FOB->GetBufferSize(), &m_oShaderPipeline_RuntimeLighting_FOB.pVertexShader ) );

	D3D11_INPUT_ELEMENT_DESC aInputElements[] = {
		{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "NORMAL", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "BLENDWEIGHT", .SemanticIndex = 0, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "BLENDINDICES", .SemanticIndex = 0, .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "TEXCOORD", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
	};

	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aInputElements,
	        TARRAYSIZE( aInputElements ),
	        m_pVSShaderBlob_BakedLighting->GetBufferPointer(),
	        m_pVSShaderBlob_BakedLighting->GetBufferSize(),
	        &m_oShaderPipeline_BakedLighting.pInputLayout
	    )
	);

	// Both shaders share the same input layout
	m_oShaderPipeline_RuntimeLighting.pInputLayout     = m_oShaderPipeline_BakedLighting.pInputLayout;
	m_oShaderPipeline_RuntimeLighting_FOB.pInputLayout = m_oShaderPipeline_BakedLighting.pInputLayout;
	m_oShaderPipeline_RuntimeLighting_FOB.pPixelShader = m_oShaderPipeline_RuntimeLighting.pPixelShader;

	m_oShaderPipeline_RuntimeLighting.SetName( "Skin_RuntimeLighting" );
	m_oShaderPipeline_BakedLighting.SetName( "Skin_BakedLighting" );
	m_oShaderPipeline_RuntimeLighting_FOB.SetName( "Skin_FOB" );

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

void remaster::SkinShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
	if ( !a_pRenderPacket || !a_pRenderPacket->GetMesh() ) return;

	TPROFILER_SCOPE();

	TSkeletonInstance*  pSkeletonInstance = a_pRenderPacket->GetSkeletonInstance();
	RenderContextD3D11* pCurrentContext   = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	SkinMesh*           pMesh             = TSTATICCAST( SkinMesh, a_pRenderPacket->GetMesh() );
	SkinMaterial*       pMaterial         = TSTATICCAST( SkinMaterial, pMesh->GetMaterial() );

	const TFLOAT flPacketAlpha = a_pRenderPacket->GetAlpha();

	// Setup renderer
	// Setup model view projection matrix
	TMatrix44 mMVP;
	mMVP.Multiply( pCurrentContext->GetProjectionMatrix(), a_pRenderPacket->GetModelViewMatrix() );
	g_pRender->VSBufferSetMat4( 0, mMVP );

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

	Toshi::TVector3 vLightDirWorld;
	vLightDirWorld.x = oWorldModelView.m_f11 * fLightDirX + oWorldModelView.m_f21 * fLightDirY + oWorldModelView.m_f31 * fLightDirZ;
	vLightDirWorld.y = oWorldModelView.m_f12 * fLightDirX + oWorldModelView.m_f22 * fLightDirY + oWorldModelView.m_f32 * fLightDirZ;
	vLightDirWorld.z = oWorldModelView.m_f13 * fLightDirX + oWorldModelView.m_f23 * fLightDirY + oWorldModelView.m_f33 * fLightDirZ;
	vLightDirWorld.Normalize();

	TVector4 vUnkVector = TVector4( 1.0f, 0.0f, 0.0f, 1.0f );

	TVector4 vLightingLerp1 = TVector4( 1.0f, 1.0f, 1.0f, 1.0f );
	TVector4 vLightingLerp2 = TVector4( 1.0f, 1.0f, 1.0f, 1.0f );

	TUINT  ui8ShadeCoeff = a_pRenderPacket->GetShadeCoeff();
	TFLOAT flShadeCoeff  = a_pRenderPacket->GetShadeCoeff() * ( 1.0f / 255.0f );

	TBOOL bUseWorldAmbientColor = TFALSE;

	if ( pMaterial->IsHDLighting() && pMaterial->HasLighting1Tex() && pMaterial->HasLighting2Tex() )
	{
		g_pRender->SetShaderPipelineState( m_oShaderPipeline_BakedLighting );

		if ( pMaterial->GetSomeTexture() )
			flShadeCoeff = flShadeCoeff - 0.3f;

		vLightingLerp1.x = 1.0f - flShadeCoeff;
		vLightingLerp1.y = 1.0f - flShadeCoeff;
		vLightingLerp1.z = 1.0f - flShadeCoeff;
		vLightingLerp1.w = 0.0f;

		vLightingLerp2.x = flShadeCoeff;
		vLightingLerp2.y = flShadeCoeff;
		vLightingLerp2.z = flShadeCoeff;
		vLightingLerp2.w = 0.0f;

		g_pRender->SetShaderResource( 1, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_0 )->GetD3DTexture() ) );
		g_pRender->SetShaderResource( 2, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_1 )->GetD3DTexture() ) );
		g_pRender->SetShaderResource( 3, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_2 )->GetD3DTexture() ) );
		g_pRender->SetShaderResource( 4, TREINTERPRETCAST( ID3D11ShaderResourceView*, pMaterial->GetLightingTexture( ASkinMaterial::LT_3 )->GetD3DTexture() ) );
		g_pRender->PSSetSamplerState( 1, 5 );
	}
	else
	{
		const TBOOL bIsFOB    = pMesh->IsFOB();
		bUseWorldAmbientColor = bIsFOB;

		g_pRender->SetShaderPipelineState( bIsFOB ? m_oShaderPipeline_RuntimeLighting_FOB : m_oShaderPipeline_RuntimeLighting );
	}

	WorldShaderDX11* pWorldShader = TSTATICCAST( WorldShaderDX11, AWorldShader::GetSingleton() );

	TMatrix44 oViewModel;
	oViewModel.Invert( a_pRenderPacket->GetModelViewMatrix() );

	TVector4 upVector;
	upVector.Negate3( oViewModel.AsBasisVector4( 2 ) );

	TVector4 vAmbientColour = a_pRenderPacket->GetAmbientColour();
	vAmbientColour.w        = flPacketAlpha;

	static TVector4 s_vecUnused1 = TVector4( 0.54509807f, 0.60784316f, 0.47058824f );

	g_pRender->VSBufferSetVec4( 7, upVector );
	g_pRender->VSBufferSetVec4( 4, TFALSE ? ( ( pWorldShader->GetAmbientColour() - pWorldShader->GetShadowColour() ) + pWorldShader->GetShadowColour() ) * s_vecUnused1 : vAmbientColour );

	// Set alpha ref
	TINT iAlphaRef = pMaterial->IsBlending() ? 1 : 128;
	TINT iAlpha    = TINT( flPacketAlpha * 128.0f );
	if ( iAlphaRef < iAlpha ) iAlpha = iAlphaRef;
	vLightColour.w = iAlpha * ( 1.0f / 255.0f );
	g_pRender->VSBufferSetVec4( 5, vLightColour );

	// Blend enable
	if ( pMaterial->GetBlendMode() != 0 || flPacketAlpha < 1.0f )
		g_pRender->SetBlendEnabled( TTRUE );
	else
		g_pRender->SetBlendEnabled( TFALSE );

	g_pRender->VSBufferSetVec4( 6, vLightDirWorld );
	g_pRender->VSBufferSetVec4( 8, vLightingLerp1 );
	g_pRender->VSBufferSetVec4( 9, vLightingLerp2 );

	// Fog settings
	TVector4 vMiscSettings;
	vMiscSettings.x = pCurrentContext->m_fFogDistanceStart;
	vMiscSettings.y = pCurrentContext->m_fFogDistanceEnd;

	TVector4 vFogColor = pCurrentContext->m_FogColor;
	vFogColor.w        = s_flFogDensity;

	g_pRender->VSBufferSetVec4( 10, vMiscSettings );
	g_pRender->VSBufferSetVec4( 11, vFogColor );

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

		// TODO: use separate buffer for bone matrices to reduce bandwidth
		// Get all bones into render buffer
		for ( TUINT k = 0; k < pSubMesh->uiNumBones; k++ )
			g_pRender->VSBufferSetMat4( 12 + k * 4, pSkeletonInstance->GetBone( pSubMesh->aBones[ k ] ).m_Transform );

		// Draw mesh
		g_pRender->DrawIndexed(
		    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
		    pIndexPool->GetNumIndices(),
		    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
		    indexBuffer.uiIndexOffset,
		    DXGI_FORMAT_R16_UINT,
		    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
		    sizeof( TTMDWin::Vertex ),
		    vertexBuffer.uiVertexOffset
		);
	}
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
