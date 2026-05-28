#include "pch.h"
#include "SysShader.h"
#include "SysMaterial.h"
#include "SysMesh.h"
#include "Generated/SystemShaderCombos.h"
#include "Resource/ClassPatcher.h"

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

TDEFINE_CLASS_PATCHED( remaster::SysShaderDX11, 0x0079a4c8 );

MEMBER_HOOK( 0x005f04e0, ASysShaderHAL, ASysShaderHAL_Constructor, remaster::SysShaderDX11* )
{
	TFree( this );
	return new remaster::SysShaderDX11();
}

void remaster::SetupRenderHooks_SysShader()
{
	InstallHook<ASysShaderHAL_Constructor>();
}

remaster::SysShaderDX11::SysShaderDX11()
{
	// Set Singleton
	*(ASysShader**)( 0x0079a340 ) = this;
}

remaster::SysShaderDX11::~SysShaderDX11()
{
}

void remaster::SysShaderDX11::Flush()
{
	Validate();

	g_pRender->SetBlendEnabled( TTRUE );
	g_pRender->SetAlphaToCoverageEnabled( TFALSE );

	m_aOrderTables[ 0 ].Render();

	g_pRender->SetBlendEnabled( TFALSE );
}

void remaster::SysShaderDX11::StartFlush()
{
	if ( !IsValidated() ) return;

	g_pRender->SetBlendEnabled( TTRUE );
	g_pRender->SetAlphaToCoverageEnabled( TFALSE );
}

void remaster::SysShaderDX11::EndFlush()
{
	g_pRender->SetBlendEnabled( TFALSE );
}

TBOOL remaster::SysShaderDX11::Create()
{
	TASSERT( TFALSE == IsCreated() );

	m_aOrderTables[ 0 ].Create( this, -1000 );
	m_aOrderTables[ 1 ].Create( this, 2000 );
	m_aOrderTables[ 2 ].Create( this, -6000 );
	m_aOrderTables[ 3 ].Create( this, -5990 );

	ASysShader::Create();
	return Validate();
}

TBOOL remaster::SysShaderDX11::Validate()
{
	if ( IsValidated() )
		return TTRUE;

	dx11::ShaderCombo& rSystemVSCombo = shadercombos::GetSystemVertexShaderCombo_vs_main();
	dx11::ShaderCombo& rSystemPSCombo = shadercombos::GetSystemPixelShaderCombo_ps_main();

	D3D11_INPUT_ELEMENT_DESC aInputElements[] = {
		{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "COLOR", .SemanticIndex = 0, .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "TEXCOORD", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
	};

	ID3D11InputLayout* pInputLayout = TNULL;
	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aInputElements,
	        TARRAYSIZE( aInputElements ),
	        rSystemVSCombo.GetBlob( 0 )->GetBufferPointer(),
	        rSystemVSCombo.GetBlob( 0 )->GetBufferSize(),
	        &pInputLayout
	    )
	);

	TASSERT( shadercombos::CreateSystemShaderPipelines( rSystemVSCombo, &rSystemPSCombo, pInputLayout, m_vecSystemPipelines, "System" ) );

	return BaseClass::Validate();
}

void remaster::SysShaderDX11::Invalidate()
{
}

TBOOL remaster::SysShaderDX11::TryInvalidate()
{
	return TFALSE;
}

TBOOL remaster::SysShaderDX11::TryValidate()
{
	return TFALSE;
}

void remaster::SysShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
	if ( !ms_bRenderEnabled ) return;

	RenderContextD3D11* pCurrentContext = TSTATICCAST( RenderContextD3D11, g_pRender->GetCurrentContext() );
	SysMesh*            pMesh           = TSTATICCAST( SysMesh, a_pRenderPacket->GetMesh() );

	// Set vertices
	TVertexPoolResource* pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
	TIndexPoolResource*  pIndexPool  = TSTATICCAST( TIndexPoolResource, pMesh->GetIndexPool() );
	TVALIDPTR( pVertexPool );
	TVALIDPTR( pIndexPool );

	TVertexBlockResource::HALBuffer vertexBuffer;
	CALL_THIS( 0x006d6660, TVertexPoolResource*, TBOOL, pVertexPool, TVertexBlockResource::HALBuffer&, vertexBuffer ); // pVertexPool->GetHALBuffer( &vertexBuffer );

	TIndexBlockResource::HALBuffer indexBuffer;
	CALL_THIS( 0x006d6180, TIndexPoolResource*, TBOOL, pIndexPool, TIndexBlockResource::HALBuffer&, indexBuffer ); // pIndexPool->GetHALBuffer( &indexBuffer );

	const TUINT uiComboFlags = ( g_pRender->PSGetShaderResource( 0 ) != TNULL ) ? shadercombos::System_TEXTURED : 0;
	g_pRender->SetShaderPipelineState( m_vecSystemPipelines[ shadercombos::GetSystemComboIndex( uiComboFlags ) ] );
	g_pRender->SetDepthBias( pMesh->GetZBias() );

	// Fill vertex constant buffer
	// Setup model view projection matrix
	TMatrix44 mMVP;
	mMVP.Multiply( pCurrentContext->GetProjectionMatrix(), a_pRenderPacket->GetModelViewMatrix() );
	g_pRender->VSBufferSetMat4( 0, mMVP );

	if ( pIndexPool->GetFlags() & 8 )
	{
		g_pRender->DrawIndexed(
		    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
		    pIndexPool->GetNumIndices(),
		    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
		    indexBuffer.uiIndexOffset,
		    DXGI_FORMAT_R16_UINT,
		    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
		    sizeof( SysMesh::Vertex ),
		    vertexBuffer.uiVertexOffset,
		    TNULL
		);
	}
	else
	{
		g_pRender->DrawIndexed(
		    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		    pIndexPool->GetNumIndices(),
		    (ID3D11Buffer*)indexBuffer.pIndexBuffer,
		    indexBuffer.uiIndexOffset,
		    DXGI_FORMAT_R16_UINT,
		    (ID3D11Buffer*)vertexBuffer.apVertexBuffers[ 0 ],
		    sizeof( SysMesh::Vertex ),
		    vertexBuffer.uiVertexOffset / 3,
		    TNULL
		);
	}

	g_pRender->SetDepthBias( 0 );
}

ASysMaterial* remaster::SysShaderDX11::CreateMaterial( const TCHAR* a_szName )
{
	TPROFILER_SCOPE();

	Validate();

	auto pMaterialHAL = new SysMaterial();
	pMaterialHAL->SetShader( this );
	pMaterialHAL->SetOrderTable( GetOrderTable( 0 ) );

	return pMaterialHAL;
}

ASysMesh* remaster::SysShaderDX11::CreateMesh( const TCHAR* a_szName )
{
	TPROFILER_SCOPE();

	Validate();

	auto pMeshHAL = new SysMesh();
	pMeshHAL->SetOwnerShader( this );

	return pMeshHAL;
}
