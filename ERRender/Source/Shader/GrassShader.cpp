#include "pch.h"
#include "GrassShader.h"
#include "GrassMesh.h"
#include "GrassMaterial.h"
#include "Resource/ClassPatcher.h"
#include "RenderDX11Utils.h"
#include "RenderContentDX11.h"
#include "WorldShader.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <BYardSDK/ACamera.h>

#include <Render/TRenderPacket.h>
#include <Platform/DX8/TRenderInterface_DX8.h>
#include <Platform/DX8/TRenderContext_DX8.h>
#include <Platform/DX8/TVertexBlockResource_DX8.h>
#include <Platform/DX8/TVertexPoolResource_DX8.h>
#include <Platform/DX8/TIndexBlockResource_DX8.h>
#include <Platform/DX8/TIndexPoolResource_DX8.h>

#include <Toshi/TScheduler.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::GrassShaderDX11, 0x0079aacc );

MEMBER_HOOK( 0x005f7c10, AGrassShaderHAL, AGrassShaderHAL_Constructor, remaster::GrassShaderDX11* )
{
	TFree( this );
	return new remaster::GrassShaderDX11();
}

void remaster::SetupRenderHooks_GrassShader()
{
	InstallHook<AGrassShaderHAL_Constructor>();
}

static TVector3     g_vecAnimationDataOffset;
static TVector4     g_vecAnimOffset;
static const TFLOAT g_fAnimationSpeed = 2.25f;

remaster::GrassShaderDX11::GrassShaderDX11()
{
	// Set Singleton
	*(AGrassShader**)( 0x0079aa24 ) = this;
}

remaster::GrassShaderDX11::~GrassShaderDX11()
{
}

void remaster::GrassShaderDX11::Flush()
{
 	Validate();

	g_pRender->SetBlendEnabled( TFALSE );
	m_oOrderTable.Render();
}

void remaster::GrassShaderDX11::StartFlush()
{
	if ( !IsValidated() ) return;

	g_pRender->PSSetSamplerState( 0, 2 );
	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetBlendEnabled( TFALSE );
		
	UpdateAnimation();


	{
// 		pD3DDevice->SetRenderState( D3DRS_ZWRITEENABLE, TRUE );
// 		pD3DDevice->SetRenderState( D3DRS_ALPHATESTENABLE, TRUE );
// 		pD3DDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
// 		pD3DDevice->SetRenderState( D3DRS_ALPHAFUNC, 5 );
// 
// 		pD3DDevice->SetTextureStageState( 0, D3DTSS_COLOROP, 4 );
// 		pD3DDevice->SetTextureStageState( 0, D3DTSS_COLORARG1, 2 );
// 		pD3DDevice->SetTextureStageState( 0, D3DTSS_COLORARG2, 0 );
// 		pD3DDevice->SetTextureStageState( 0, D3DTSS_ALPHAOP, 4 );
// 		pD3DDevice->SetTextureStageState( 0, D3DTSS_ALPHAARG1, 2 );
// 		pD3DDevice->SetTextureStageState( 0, D3DTSS_ALPHAARG2, 0 );
// 		pD3DDevice->SetTextureStageState( 1, D3DTSS_COLOROP, 1 );
// 		pD3DDevice->SetTextureStageState( 1, D3DTSS_ALPHAOP, 1 );

	}
}

void remaster::GrassShaderDX11::EndFlush()
{
	g_pRender->SetBlendEnabled( TTRUE );

// 	TRenderD3DInterface* pRenderInterface = TRenderD3DInterface::Interface();
// 	IDirect3DDevice8*    pD3DDevice       = pRenderInterface->GetDirect3DDevice();
// 
// 	pD3DDevice->SetRenderState( D3DRS_SPECULARENABLE, FALSE );
// 	pD3DDevice->SetRenderState( D3DRS_FOGENABLE, FALSE );
// 	pD3DDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
}

TBOOL remaster::GrassShaderDX11::Create()
{
	m_oOrderTable.Create( this, -6999 );
	AGrassShader::Create();

	return Validate();
}

TBOOL remaster::GrassShaderDX11::Validate()
{
	if ( IsValidated() )
		return TTRUE;

	//D3D_SHADER_MACRO aTexturedShaderMacro[] = { "TEXTURED", "1", TNULL, TNULL };

	m_pVSShaderBlob = dx11::CompileShaderFromFile( "Data\\Shaders\\Grass.hlsl", "vs_main", "vs_5_0", TNULL );
	m_pPSShaderBlob = dx11::CompileShaderFromFile( "Data\\Shaders\\Grass.hlsl", "ps_main", "ps_5_0", TNULL );

	TASSERT( m_pVSShaderBlob && m_pPSShaderBlob );
	DX11_API_VALIDATE( dx11::CreateVertexShader( m_pVSShaderBlob->GetBufferPointer(), m_pVSShaderBlob->GetBufferSize(), &m_oShaderPipeline.pVertexShader ) );
	DX11_API_VALIDATE( dx11::CreatePixelShader( m_pPSShaderBlob->GetBufferPointer(), m_pPSShaderBlob->GetBufferSize(), &m_oShaderPipeline.pPixelShader ) );

	D3D11_INPUT_ELEMENT_DESC aInputElements[] = {
		{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "NORMAL", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "COLOR", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
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

	m_oShaderPipeline.SetName( "Grass" );

	return BaseClass::Validate();
}

void remaster::GrassShaderDX11::Invalidate()
{
}

TBOOL remaster::GrassShaderDX11::TryInvalidate()
{
	return TFALSE;
}

TBOOL remaster::GrassShaderDX11::TryValidate()
{
	return TFALSE;
}

ID3D11ShaderResourceView* g_pGrassTexture = TNULL;

void remaster::GrassShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
	if ( !a_pRenderPacket || !a_pRenderPacket->GetMesh() ) return;

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	GrassMesh*          pMesh           = TSTATICCAST( GrassMesh, a_pRenderPacket->GetMesh() );
	GrassMaterial*      pMaterial       = TSTATICCAST( GrassMaterial, pMesh->GetMaterial() );

	g_pRender->SetShaderPipelineState( m_oShaderPipeline );

	// Fill vertex constant buffer
	// Setup model view projection matrix
	TMatrix44 mMVP;
	mMVP.Multiply( pCurrentContext->GetProjectionMatrix(), a_pRenderPacket->GetModelViewMatrix() );
	g_pRender->VSBufferSetMat4( 0, mMVP );

	// Setup UV offset
	g_vecAnimOffset.w = 1.0f;
	g_pRender->VSBufferSetVec4( 4, g_vecAnimOffset );

	// Setup colors
	WorldShaderDX11* pWorldShader = TSTATICCAST( WorldShaderDX11, AWorldShader::GetSingleton() );

	g_pRender->VSBufferSetVec4( 5, pWorldShader->GetAmbientColour() );
	g_pRender->VSBufferSetVec4( 6, pWorldShader->GetShadowColour() );

	// Setup model normal offset
	TVector4 vecOffset = TVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	g_pRender->VSBufferSetVec4( 7, vecOffset );

	g_pRender->SetShaderResource( 0, g_pGrassTexture );

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
	    vertexBuffer.uiVertexOffset
	);

	// Layers...
	ACamera* pCamera   = ACameraManager::GetSingleton()->GetCurrentCamera();
	TVector4 vecCamPos = pCamera->m_Matrix.GetTranslation();

	// Transform coordinates to the actual world position, since they are exported rotated
	TVector4 vecMeshBounding;
	vecMeshBounding.x = pMesh->GetCellMeshSphere()->m_BoundingSphere.AsVector4().x;
	vecMeshBounding.y = -pMesh->GetCellMeshSphere()->m_BoundingSphere.AsVector4().z;
	vecMeshBounding.z = pMesh->GetCellMeshSphere()->m_BoundingSphere.AsVector4().y;
	vecMeshBounding.w = pMesh->GetCellMeshSphere()->m_BoundingSphere.GetRadius();
	
	TFLOAT fDistanceToCamera = TVector4::DistanceXZ( vecMeshBounding, vecCamPos ) - vecMeshBounding.w;

	if ( fDistanceToCamera <= 100.0f )
	{
		constexpr TINT MAX_LAYERS = 5;
		TINT           iNumLayers = MAX_LAYERS;

		if ( fDistanceToCamera > 20.0f )
		{
			iNumLayers = ( fDistanceToCamera >= 50.0f ) ?
			    1 :
			    MAX_LAYERS - 1 - TMath::Min( TINT( ( fDistanceToCamera - 20.0f ) / 10.0f ), MAX_LAYERS - 2 );
		}

		g_vecAnimOffset.w = 2.0f;
		TFLOAT fStepSize  = 0.25f / iNumLayers;

		g_pRender->VSBufferSetVec4( 4, g_vecAnimOffset );

		for ( TINT i = 1; i <= iNumLayers; i++ )
		{
			T2Texture** g_aGrassLayers = TREINTERPRETCAST( T2Texture**, 0x007b4638 );

			g_pRender->SetShaderResource( 0, (ID3D11ShaderResourceView*)g_aGrassLayers[ i ]->GetD3DTexture() );

			vecOffset.x += fStepSize;
			vecOffset.y += fStepSize;
			vecOffset.z += fStepSize;
			g_pRender->VSBufferSetVec4( 7, vecOffset );

			// Draw layer
			g_pRender->DrawIndexed(
			    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
			    pIndexPool->GetNumIndices(),
			    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
			    indexBuffer.uiIndexOffset,
			    DXGI_FORMAT_R16_UINT,
			    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
			    sizeof( WorldVertex ),
			    vertexBuffer.uiVertexOffset
			);
		}
	}
}

AGrassMaterial* remaster::GrassShaderDX11::CreateMaterial( const TCHAR* a_szName )
{
	Validate();

	GrassMaterial* pMaterial = new GrassMaterial();
	pMaterial->SetShader( this );

	m_oOrderTable.RegisterMaterial( pMaterial );
	return pMaterial;
}

AGrassMesh* remaster::GrassShaderDX11::CreateMesh( const TCHAR* a_szName )
{
	Validate();

	GrassMesh* pMesh = new GrassMesh();
	pMesh->SetOwnerShader( this );

	return pMesh;
}

void remaster::GrassShaderDX11::UpdateAnimation()
{
	TSystemManager* g_pSystemManager = TREINTERPRETCAST( TSystemManager*, 0x007ce640 );

	TFLOAT fCurrentTime = g_pSystemManager->GetScheduler()->GetTotalTime();

	TVector3 vecAnim = TVector3(
	    fCurrentTime * 0.70f * g_fAnimationSpeed,
	    fCurrentTime * 0.92f * g_fAnimationSpeed,
	    fCurrentTime * 0.20f * g_fAnimationSpeed
	);

	TMath::NormaliseAngle( vecAnim.x );
	TMath::NormaliseAngle( vecAnim.y );
	TMath::NormaliseAngle( vecAnim.z );

	g_vecAnimationDataOffset.x = TMath::Sin( vecAnim.x );
	g_vecAnimationDataOffset.y = TMath::Sin( vecAnim.y );
	g_vecAnimationDataOffset.z = TMath::Sin( vecAnim.z );
	g_vecAnimationDataOffset.Multiply( 0.075f );

	g_vecAnimOffset.x = g_vecAnimationDataOffset.x;
	g_vecAnimOffset.y = g_vecAnimationDataOffset.y;
	g_vecAnimOffset.z = g_vecAnimationDataOffset.z;
}
