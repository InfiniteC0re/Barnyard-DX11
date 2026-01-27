#include "pch.h"
#include "SkinShader.h"
#include "SkinMaterial.h"
#include "SkinMesh.h"
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
}

void remaster::SkinShaderDX11::StartFlush()
{
	m_oWorldViewMatrix = g_pRender->GetCurrentContext()->GetWorldViewMatrix();
	m_oViewWorldMatrix.Invert( m_oWorldViewMatrix );
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

	m_pVSShaderBlob = dx11::CompileShaderFromFile( "Data\\Shaders\\Skin.hlsl", "vs_main", "vs_5_0", TNULL );
	m_pPSShaderBlob = dx11::CompileShaderFromFile( "Data\\Shaders\\Skin.hlsl", "ps_main", "ps_5_0", TNULL );

	TASSERT( m_pVSShaderBlob && m_pPSShaderBlob );
	DX11_API_VALIDATE( dx11::CreateVertexShader( m_pVSShaderBlob->GetBufferPointer(), m_pVSShaderBlob->GetBufferSize(), &m_oShaderPipeline.pVertexShader ) );
	DX11_API_VALIDATE( dx11::CreatePixelShader( m_pPSShaderBlob->GetBufferPointer(), m_pPSShaderBlob->GetBufferSize(), &m_oShaderPipeline.pPixelShader ) );

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
	        m_pVSShaderBlob->GetBufferPointer(),
	        m_pVSShaderBlob->GetBufferSize(),
	        &m_oShaderPipeline.pInputLayout
	    )
	);

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

	TSkeletonInstance*  pSkeletonInstance = a_pRenderPacket->GetSkeletonInstance();
	RenderContextD3D11* pCurrentContext   = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	SkinMesh*           pMesh             = TSTATICCAST( SkinMesh, a_pRenderPacket->GetMesh() );
	SkinMaterial*       pMaterial         = TSTATICCAST( SkinMaterial, pMesh->GetMaterial() );

	const TFLOAT flPacketAlpha = a_pRenderPacket->GetAlpha();

	g_pRender->SetShaderPipelineState( m_oShaderPipeline );

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

	if ( pMaterial->IsHDLighting() && pMaterial->HasLighting1Tex() && pMaterial->HasLighting2Tex() )
	{
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

	TMatrix44 oViewModel;
	oViewModel.Invert( a_pRenderPacket->GetModelViewMatrix() );

	TVector4 upVector;
	upVector.Negate3( oViewModel.AsBasisVector4( 2 ) );

	TVector4 vAmbientColour = a_pRenderPacket->GetAmbientColour();
	vAmbientColour.w        = flPacketAlpha;

	g_pRender->VSBufferSetVec4( 6, upVector );
	g_pRender->VSBufferSetVec4( 4, vAmbientColour );
	g_pRender->VSBufferSetVec4( 5, vLightDirWorld );
	g_pRender->VSBufferSetVec4( 7, vLightingLerp1 );
	g_pRender->VSBufferSetVec4( 8, vLightingLerp2 );

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
		for ( TINT k = 0; k < pSubMesh->uiNumBones; k++ )
			g_pRender->VSBufferSetMat4( 9 + k * 4, pSkeletonInstance->GetBone( pSubMesh->aBones[ k ] ).m_Transform );

		// Draw mesh
		g_pRender->DrawIndexed(
		    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
		    pIndexPool->GetNumIndices(),
		    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
		    indexBuffer.uiIndexOffset,
		    DXGI_FORMAT_R16_UINT,
		    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
		    sizeof( TTMDWin::SkinVertex ),
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
