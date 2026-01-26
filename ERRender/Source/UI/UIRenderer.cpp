#include "pch.h"
#include "UIRenderer.h"
#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "FontRenderer.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <BYardSDK/AGUI2.h>
#include <BYardSDK/AMaterialLibraryManager.h>

#include <Platform/DX8/TTextureResourceHAL_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x0064e5c0, AGUI2Renderer, AGUI2Renderer_Constructor, remaster::UIRendererDX11* )
{
	return new remaster::UIRendererDX11();
}

void remaster::SetupRenderHooks_UIRenderer()
{
	InstallHook<AGUI2Renderer_Constructor>();
}

remaster::UIRendererDX11* remaster::g_pUIRender = TNULL;

remaster::UIRendererDX11::UIRendererDX11()
{
	m_pTransforms       = new AGUI2Transform[ MAX_NUM_TRANSFORMS ];
	m_iTransformCount   = 0;
	m_bIsTransformDirty = TFALSE;

	D3D_SHADER_MACRO aTexturedShaderMacro[] = { "TEXTURED", "1", TNULL, TNULL };
	D3D_SHADER_MACRO aFontShaderMacro[]     = { "FONT", "1", TNULL, TNULL };

	m_pVSShaderBlob          = dx11::CompileShaderFromFile( "Data\\Shaders\\UI.hlsl", "vs_main", "vs_5_0", TNULL );
	m_pPSShaderBlob_Textured = dx11::CompileShaderFromFile( "Data\\Shaders\\UI.hlsl", "ps_main", "ps_5_0", aTexturedShaderMacro );
	m_pPSShaderBlob_Font     = dx11::CompileShaderFromFile( "Data\\Shaders\\UI.hlsl", "ps_main", "ps_5_0", aFontShaderMacro );
	m_pPSShaderBlob_Solid    = dx11::CompileShaderFromFile( "Data\\Shaders\\UI.hlsl", "ps_main", "ps_5_0", TNULL );

	TASSERT( m_pVSShaderBlob && m_pPSShaderBlob_Textured );
	DX11_API_VALIDATE( dx11::CreateVertexShader( m_pVSShaderBlob->GetBufferPointer(), m_pVSShaderBlob->GetBufferSize(), &m_pVertexShader ) );
	DX11_API_VALIDATE( dx11::CreatePixelShader( m_pPSShaderBlob_Textured->GetBufferPointer(), m_pPSShaderBlob_Textured->GetBufferSize(), &m_pPixelShader_Textured ) );
	DX11_API_VALIDATE( dx11::CreatePixelShader( m_pPSShaderBlob_Solid->GetBufferPointer(), m_pPSShaderBlob_Solid->GetBufferSize(), &m_pPixelShader_Solid ) );
	DX11_API_VALIDATE( dx11::CreatePixelShader( m_pPSShaderBlob_Font->GetBufferPointer(), m_pPSShaderBlob_Font->GetBufferSize(), &m_pPixelShader_Font ) );

	D3D11_INPUT_ELEMENT_DESC aInputElements[] = {
		{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "COLOR", .SemanticIndex = 0, .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "TEXCOORD", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
	};

	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aInputElements,
	        TARRAYSIZE( aInputElements ),
	        m_pVSShaderBlob->GetBufferPointer(),
	        m_pVSShaderBlob->GetBufferSize(),
	        &m_pInputLayout
	    )
	);

	fontrenderer::Create();
	g_pUIRender = this;
}

remaster::UIRendererDX11::~UIRendererDX11()
{
	delete[] m_pTransforms;

	fontrenderer::Destroy();

	g_pUIRender = TNULL;
}

AGUI2Material* remaster::UIRendererDX11::CreateMaterial( Toshi::TTexture* a_pTexture )
{
	auto pMaterial = new AGUI2Material;

	pMaterial->Create_Hack();
	pMaterial->m_pTextureResource = a_pTexture;

	return pMaterial;
}

AGUI2Material* remaster::UIRendererDX11::CreateMaterialFromName( const TCHAR* a_szTextureName )
{
	return CreateMaterial(
	    GetTexture( a_szTextureName )
	);
}

TUINT remaster::UIRendererDX11::GetWidth( AGUI2Material* a_pMaterial )
{
	return a_pMaterial->m_pTextureResource->GetWidth();
}

TUINT remaster::UIRendererDX11::GetHeight( AGUI2Material* a_pMaterial )
{
	return a_pMaterial->m_pTextureResource->GetHeight();
}

void remaster::UIRendererDX11::BeginScene()
{
	TPROFILER_SCOPE();
	TRenderInterface::DISPLAYPARAMS* pDisplayParams = g_pRender->GetCurrentDisplayParams();

	m_oViewport = {
		.TopLeftX = 0,
		.TopLeftY = 0,
		.Width    = TFLOAT( pDisplayParams->uiWidth ),
		.Height   = TFLOAT( pDisplayParams->uiHeight ),
		.MinDepth = 0.0f,
		.MaxDepth = 1.0f
	};

	// Calculate UI scale
	TFLOAT flUICanvasWidth;
	TFLOAT flUICanvasHeight;
	AGUI2::GetSingleton()->GetDimensions( flUICanvasWidth, flUICanvasHeight );

	m_flUIScaleX = ( remaster::g_pRender->GetSurfaceWidth() / flUICanvasWidth );
	m_flUIScaleY = ( remaster::g_pRender->GetSurfaceHeight() / flUICanvasHeight );

	// Update projection matrix
	SetupProjectionMatrix( m_matProjection, 0.0f, TFLOAT( pDisplayParams->uiWidth ), 0.0f, TFLOAT( pDisplayParams->uiHeight ) );
	g_pRender->VSBufferSetMat4( 0, m_matProjection );

	// Initialise first root transform
	TFLOAT fRootWidth;
	TFLOAT fRootHeight;
	AGUI2::GetContext()->GetRootElement()->GetDimensions( fRootWidth, fRootHeight );

	AGUI2Transform& rTransform    = m_pTransforms[ m_iTransformCount ];
	rTransform.m_aMatrixRows[ 0 ] = { pDisplayParams->uiWidth / fRootWidth, 0.0f };
	rTransform.m_aMatrixRows[ 1 ] = { 0.0f, -TFLOAT( pDisplayParams->uiHeight ) / fRootHeight };
	rTransform.m_vecTranslation   = { 0.0f, 0.0f };

	g_pRender->SetPixelShader( m_pPixelShader_Solid );
	g_pRender->SetVertexShader( m_pVertexShader );
	g_pRender->SetInputLayout( m_pInputLayout );

	g_pRender->SetCullMode( D3D11_CULL_NONE );
	g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
	g_pRender->SetZMode( g_pRender->IsZEnabled(), D3D11_COMPARISON_ALWAYS, D3D11_DEPTH_WRITE_MASK_ZERO );

	SetColour( TCOLOR( 255, 255, 255 ) );
	m_pMaterial = TNULL;
}

void remaster::UIRendererDX11::EndScene()
{
	TPROFILER_SCOPE();

	g_pRender->SetZMode( g_pRender->IsZEnabled(), D3D11_COMPARISON_LESS_EQUAL, D3D11_DEPTH_WRITE_MASK_ALL );

	// Run garbage collection of unused font renderer objects
	fontrenderer::Update();
}

void remaster::UIRendererDX11::ResetRenderer()
{
}

void remaster::UIRendererDX11::PrepareRenderer()
{
	g_pRender->SetVertexShader( m_pVertexShader );
	g_pRender->SetInputLayout( m_pInputLayout );

	g_pRender->SetCullMode( D3D11_CULL_NONE );
	g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
	g_pRender->SetZMode( g_pRender->IsZEnabled(), D3D11_COMPARISON_ALWAYS, D3D11_DEPTH_WRITE_MASK_ZERO );

	sm_fZCoordinate = 0.1f;

	// Force material to update
	auto pMaterial = m_pMaterial;
	m_pMaterial    = (AGUI2Material*)( ~(uintptr_t)pMaterial );
	SetMaterial( pMaterial );

	// Force colour to update
	auto uiColour = m_uiColour;
	m_uiColour    = ~uiColour;
	SetColour( uiColour );

	m_bIsTransformDirty = TTRUE;
}

void remaster::UIRendererDX11::SetMaterial( AGUI2Material* a_pMaterial )
{
	if ( a_pMaterial == m_pMaterial ) return;

	g_pRender->SetCullMode( D3D11_CULL_NONE );
	sm_fZCoordinate = 0.1f;

	if ( a_pMaterial == TNULL )
	{
		SetTextureResourceView( TNULL );
		g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
	}
	else
	{
		auto pTexture = TSTATICCAST( Toshi::TTextureResourceHAL, a_pMaterial->m_pTextureResource );
		pTexture->Validate();

		SetTextureResourceView( (ID3D11ShaderResourceView*)pTexture->GetD3DTexture() );

		switch ( a_pMaterial->m_eBlendState )
		{
			case 0:
				g_pRender->SetBlendEnabled( TFALSE );
				break;
			case 1:
				g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
				g_pRender->SetDepthWrite( TFALSE );
				break;
			case 2:
				g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE );
				g_pRender->SetDepthWrite( TFALSE );
				break;
			case 3:
				g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_REV_SUBTRACT, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE );
				g_pRender->SetDepthWrite( TFALSE );
				break;
			case 4:
				g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE );
				g_pRender->SetZMode( TTRUE, D3D11_COMPARISON_LESS_EQUAL, D3D11_DEPTH_WRITE_MASK_ALL );
				sm_fZCoordinate = ( !sm_bUnknownFlag ) ? 0.05f : 0.02f;
				break;
			case 5:
				g_pRender->SetBlendMode( TTRUE, g_pRender->GetBlendOp(), D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
				g_pRender->SetZMode( TTRUE, D3D11_COMPARISON_LESS_EQUAL, D3D11_DEPTH_WRITE_MASK_ZERO );

				if ( sm_bUnknownFlag )
				{
					sm_fZCoordinate = 0.03f;
					sm_bUnknownFlag = TFALSE;
				}

				break;
			case 6:
				g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
				g_pRender->SetZMode( g_pRender->IsZEnabled(), D3D11_COMPARISON_LESS_EQUAL, D3D11_DEPTH_WRITE_MASK_ALL );
				sm_fZCoordinate = 0.04f;
				sm_bUnknownFlag = TTRUE;
				break;
			default:
				g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA );
				break;
		}

		if ( a_pMaterial->m_eTextureAddress == 1 )
		{
			g_pRender->PSSetSamplerState( 0, 1 );
		}
		else if ( a_pMaterial->m_eTextureAddress == 2 )
		{
			g_pRender->PSSetSamplerState( 0, 4 );
		}
		else
		{
			g_pRender->PSSetSamplerState( 0, 3 );
		}
	}

	SetShaderType( ST_TEXTURED );
	m_pMaterial = a_pMaterial;
}

void remaster::UIRendererDX11::PushTransform( const AGUI2Transform& a_rTransform, const Toshi::TVector2& a_rVec1, const Toshi::TVector2& a_rVec2 )
{
	TASSERT( m_iTransformCount < MAX_NUM_TRANSFORMS );
	auto pTransform = m_pTransforms + ( m_iTransformCount++ );

	AGUI2Transform transform1 = *pTransform;
	AGUI2Transform transform2 = a_rTransform;

	TVector2 vec;
	transform1.Transform( vec, a_rVec1 );
	transform1.m_vecTranslation = { vec.x, vec.y };

	transform2.Transform( vec, a_rVec2 );
	transform2.m_vecTranslation = { vec.x, vec.y };

	AGUI2Transform::Multiply( m_pTransforms[ m_iTransformCount ], transform1, transform2 );
	m_bIsTransformDirty = TTRUE;
}

void remaster::UIRendererDX11::PopTransform()
{
	TASSERT( m_iTransformCount >= 0 );
	m_iTransformCount -= 1;
	m_bIsTransformDirty = TTRUE;
}

void remaster::UIRendererDX11::SetTransform( const AGUI2Transform& a_rTransform )
{
	m_pTransforms[ m_iTransformCount ] = a_rTransform;
	m_bIsTransformDirty                = TTRUE;
}

void remaster::UIRendererDX11::SetColour( TUINT32 a_uiColour )
{
	m_uiColour = a_uiColour;
}

void remaster::UIRendererDX11::SetScissor( TFLOAT a_fVal1, TFLOAT a_fVal2, TFLOAT a_fVal3, TFLOAT a_fVal4 )
{
	auto pDisplayParams = g_pRender->GetCurrentDisplayParams();
	auto pTransform     = m_pTransforms + m_iTransformCount;

	TVector2 transformed1;
	pTransform->Transform( transformed1, { a_fVal1, a_fVal2 } );

	TVector2 transformed2;
	pTransform->Transform( transformed2, { a_fVal3, a_fVal4 } );

	transformed1.x = ( pDisplayParams->uiWidth / 2.0f ) + transformed1.x;
	transformed2.x = ( pDisplayParams->uiWidth / 2.0f ) + transformed2.x;
	transformed1.y = ( pDisplayParams->uiHeight / 2.0f ) - transformed1.y;
	transformed2.y = ( pDisplayParams->uiHeight / 2.0f ) - transformed2.y;

	DWORD iLeft   = TMath::Max( TMath::FloorToInt( transformed1.x ), 0 );
	DWORD iRight  = TMath::Min( TMath::CeilToInt( transformed2.x ), TINT( pDisplayParams->uiWidth ) );
	DWORD iTop    = TMath::Max( TMath::FloorToInt( transformed2.y ), 0 );
	DWORD iBottom = TMath::Min( TMath::FloorToInt( transformed1.y ), TINT( pDisplayParams->uiHeight ) );

	m_oViewport = {
		.TopLeftX = TFLOAT( iLeft ),
		.TopLeftY = TFLOAT( iTop ),
		.Width    = TFLOAT( iRight - iLeft ),
		.Height   = TFLOAT( iBottom - iTop ),
		.MinDepth = 0.0f,
		.MaxDepth = 1.0f
	};

	if ( m_oViewport.Width == 0 )
		m_oViewport.Width = 1;

	if ( m_oViewport.Height == 0 )
		m_oViewport.Height = 1;

	g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &m_oViewport );

	SetupProjectionMatrix( m_matProjection, TFLOAT( iLeft ), TFLOAT( iRight ), TFLOAT( iTop ), TFLOAT( iBottom ) );
	g_pRender->VSBufferSetMat4( 0, m_matProjection );
}

void remaster::UIRendererDX11::ClearScissor()
{
	auto pDisplayParams = g_pRender->GetCurrentDisplayParams();

	m_oViewport = {
		.TopLeftX = 0,
		.TopLeftY = 0,
		.Width    = TFLOAT( pDisplayParams->uiWidth ),
		.Height   = TFLOAT( pDisplayParams->uiHeight ),
		.MinDepth = 0.0f,
		.MaxDepth = 1.0f
	};

	g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &m_oViewport );

	SetupProjectionMatrix( m_matProjection, m_oViewport.TopLeftX, m_oViewport.Width + m_oViewport.TopLeftX, m_oViewport.TopLeftY, m_oViewport.Height + m_oViewport.TopLeftY );
	g_pRender->VSBufferSetMat4( 0, m_matProjection );
}

void remaster::UIRendererDX11::RenderRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b, const Toshi::TVector2& uv1, const Toshi::TVector2& uv2 )
{
	UpdateTransform();

	sm_Vertices[ 0 ].Position = { a.x, a.y, sm_fZCoordinate };
	sm_Vertices[ 0 ].Colour   = m_uiColour;
	sm_Vertices[ 0 ].UV       = { uv1.x, uv1.y };

	sm_Vertices[ 1 ].Position = { b.x, a.y, sm_fZCoordinate };
	sm_Vertices[ 1 ].Colour   = m_uiColour;
	sm_Vertices[ 1 ].UV       = { uv2.x, uv1.y };

	sm_Vertices[ 2 ].Position = { a.x, b.y, sm_fZCoordinate };
	sm_Vertices[ 2 ].Colour   = m_uiColour;
	sm_Vertices[ 2 ].UV       = { uv1.x, uv2.y };

	sm_Vertices[ 3 ].Position = { b.x, b.y, sm_fZCoordinate };
	sm_Vertices[ 3 ].Colour   = m_uiColour;
	sm_Vertices[ 3 ].UV       = { uv2.x, uv2.y };

	TUINT16 aIndices[] = { 0, 1, 2, 3 };
	g_pRender->DrawImmediately( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 4, aIndices, DXGI_FORMAT_R16_UINT, &sm_Vertices, sizeof( Vertex ), 4 );
}

void remaster::UIRendererDX11::RenderTriStrip( Toshi::TVector2* vertices, Toshi::TVector2* UV, uint32_t numverts )
{
}

void remaster::UIRendererDX11::RenderLine( const Toshi::TVector2& a, const Toshi::TVector2& b )
{
}

void remaster::UIRendererDX11::RenderOutlineRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b )
{
}

void remaster::UIRendererDX11::RenderFilledRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b )
{
}

void remaster::UIRendererDX11::ScaleCoords( TFLOAT& x, TFLOAT& y )
{
}

void remaster::UIRendererDX11::ResetZCoordinate()
{
	sm_fZCoordinate = 0.01f;
}

void remaster::UIRendererDX11::SetTextureResourceView( ID3D11ShaderResourceView* a_pTextureRV )
{
	m_bHasTextureRV = a_pTextureRV != TNULL;
	g_pRender->SetShaderResource( 0, a_pTextureRV );
}

void remaster::UIRendererDX11::SetShaderType( SHADER_TYPE a_eShaderType )
{
	switch ( a_eShaderType )
	{
		case ST_TEXTURED:
			g_pRender->SetPixelShader( m_bHasTextureRV ? m_pPixelShader_Textured : m_pPixelShader_Solid );
			break;
		case ST_FONT:
			g_pRender->SetPixelShader( m_pPixelShader_Font );
			break;
		case ST_SOLID:
			g_pRender->SetPixelShader( m_pPixelShader_Solid );
			break;
	}
}

void remaster::UIRendererDX11::UpdateTransformImpl()
{
	AGUI2Transform* pTransform = m_pTransforms + m_iTransformCount;

	m_matView.m_f11 = pTransform->m_aMatrixRows[ 0 ].x;
	m_matView.m_f12 = pTransform->m_aMatrixRows[ 0 ].y;
	m_matView.m_f13 = 0.0f;
	m_matView.m_f14 = 0.0f;

	m_matView.m_f21 = pTransform->m_aMatrixRows[ 1 ].x;
	m_matView.m_f22 = pTransform->m_aMatrixRows[ 1 ].y;
	m_matView.m_f23 = 0.0f;
	m_matView.m_f24 = 0.0f;

	m_matView.m_f31 = 0.0f;
	m_matView.m_f32 = 0.0f;
	m_matView.m_f33 = 1.0f;
	m_matView.m_f34 = 0.0f;

	m_matView.m_f41 = pTransform->m_vecTranslation.x;
	m_matView.m_f42 = pTransform->m_vecTranslation.y;
	m_matView.m_f43 = 0.0f;
	m_matView.m_f44 = 1.0f;

	g_pRender->VSBufferSetMat4( 4, m_matView );
	m_bIsTransformDirty = TFALSE;
}

void remaster::UIRendererDX11::SetupProjectionMatrix( Toshi::TMatrix44& a_rOutMatrix, TFLOAT a_iLeft, TFLOAT a_iRight, TFLOAT a_iTop, TFLOAT a_iBottom )
{
	auto pDisplayParams = g_pRender->GetCurrentDisplayParams();

	a_rOutMatrix = {
		2.0f / ( a_iRight - a_iLeft ), 0.0f, 0.0f, 0.0f,
		0.0f, 2.0f / ( a_iBottom - a_iTop ), 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		( ( pDisplayParams->uiWidth - a_iLeft ) - a_iRight ) / (TFLOAT)( a_iRight - a_iLeft ), ( ( pDisplayParams->uiHeight - a_iTop ) - a_iBottom ) / (TFLOAT)( a_iTop - a_iBottom ), 0.0f, 1.0f
	};
}

void remaster::UIRendererDX11::RenderLine( TFLOAT x1, TFLOAT y1, TFLOAT x2, TFLOAT y2 )
{
}

void remaster::UIRendererDX11::DestroyMaterial( AGUI2Material* a_pMaterial )
{
	if ( a_pMaterial )
		a_pMaterial->Destroy();
}

Toshi::TTexture* remaster::UIRendererDX11::GetTexture( const TCHAR* a_szTextureName )
{
	return AMaterialLibraryManager::GetSingleton()->FindTexture( a_szTextureName );
}
