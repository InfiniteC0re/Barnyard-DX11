#include "pch.h"
#include "WorldShader.h"
#include "WorldMaterial.h"
#include "WorldMesh.h"
#include "Resource/ClassPatcher.h"
#include "Ref/AWorld.h"

#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "RenderContentDX11.h"

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
}

void remaster::WorldShaderDX11::StartFlush()
{
	if ( !IsValidated() ) return;

	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetBlendEnabled( TTRUE );
	g_pRender->SetCullMode( TFALSE ? D3D11_CULL_BACK : D3D11_CULL_FRONT );

	g_pRender->SetShaderPipelineState( m_oShaderPipeline );
}

void remaster::WorldShaderDX11::EndFlush()
{
	g_pRender->SetShaderResource( 0, TNULL );
	g_pRender->SetShaderResource( 1, TNULL );
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

	return BaseClass::Create();
}

TBOOL remaster::WorldShaderDX11::Validate()
{
	if ( IsValidated() )
		return TTRUE;

	m_pVSShaderBlob = dx11::CompileShaderFromFile( "Data\\Shaders\\World.hlsl", "vs_main", "vs_5_0", TNULL );
	m_pPSShaderBlob = dx11::CompileShaderFromFile( "Data\\Shaders\\World.hlsl", "ps_main", "ps_5_0", TNULL );

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
	
	BaseClass::Validate();

	return TFALSE;
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

void remaster::WorldShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
	if ( !a_pRenderPacket || !a_pRenderPacket->GetMesh() ) return;

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	AWorldMeshHAL*      pMesh           = TSTATICCAST( AWorldMeshHAL, a_pRenderPacket->GetMesh() );
	auto                pMaterial       = TSTATICCAST( AWorldMaterialHAL, pMesh->GetMaterial() );

	if ( pMaterial->GetBlendMode() != 0 || a_pRenderPacket->GetAlpha() < 1.0f )
		g_pRender->SetBlendEnabled( TTRUE );
	else
		g_pRender->SetBlendEnabled( TFALSE );

	TMatrix44 mMVP;
	mMVP.Multiply( pCurrentContext->GetProjectionMatrix(), a_pRenderPacket->GetModelViewMatrix() );

	g_pRender->VSBufferSetVec4( 0, &mMVP, 4 );

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
