#include "pch.h"
#include "RmlRenderInterfaceDX11.h"
#include "RenderDX11Utils.h"
#include "UI/UIRenderer.h"
#include "Generated/UIShaderCombos.h"

#include "SOIL2/stb_image.h"

#include <nanosvg.h>
#include <nanosvgrast.h>

#include <RmlUi/Core/StringUtilities.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster
{

struct RmlGeometry
{
	ID3D11Buffer* pVertexBuffer;
	ID3D11Buffer* pIndexBuffer;
	TUINT         uiNumIndices;
};

// Matches the UI.hlsl input layout: float3 position, B8G8R8A8 colour, float2 uv
struct RmlEngineVertex
{
	TFLOAT  x, y, z;
	TUINT32 uiColour;
	TFLOAT  u, v;
};

} // namespace remaster

TBOOL remaster::RmlRenderInterfaceDX11::Create()
{
	dx11::ShaderCombo& rVSCombo = shadercombos::GetUIVertexShaderCombo_vs_main();
	dx11::ShaderCombo& rPSCombo = shadercombos::GetUIPixelShaderCombo_ps_main();

	D3D11_INPUT_ELEMENT_DESC aInputElements[] = {
		{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "COLOR", .SemanticIndex = 0, .Format = DXGI_FORMAT_B8G8R8A8_UNORM, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
		{ .SemanticName = "TEXCOORD", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
	};

	DX11_API_VALIDATE(
	    g_pRender->GetD3D11Device()->CreateInputLayout(
	        aInputElements, TARRAYSIZE( aInputElements ),
	        rVSCombo.GetBlob( 0 )->GetBufferPointer(), rVSCombo.GetBlob( 0 )->GetBufferSize(),
	        &m_pInputLayout
	    )
	);

	return shadercombos::CreateUIShaderPipelines( rVSCombo, &rPSCombo, m_pInputLayout, m_vecPipelines, "RmlUI" );
}

void remaster::RmlRenderInterfaceDX11::RegisterFontTexture( ID3D11ShaderResourceView* a_pTexture )
{
	if ( a_pTexture )
		m_vecFontTextures.PushBack( a_pTexture );
}

TBOOL remaster::RmlRenderInterfaceDX11::IsFontTexture( ID3D11ShaderResourceView* a_pTexture ) const
{
	for ( TINT i = 0; i < m_vecFontTextures.Size(); i++ )
		if ( m_vecFontTextures[ i ] == a_pTexture )
			return TTRUE;

	return TFALSE;
}

void remaster::RmlRenderInterfaceDX11::Destroy()
{
	if ( m_pInputLayout )
	{
		m_pInputLayout->Release();
		m_pInputLayout = TNULL;
	}
	m_vecPipelines.Clear();
}

void remaster::RmlRenderInterfaceDX11::BeginFrame()
{
	// Project native pixels so glyphs rasterise at full resolution; layout scaling
	// is done through RmlUi's dp ratio, not by stretching the render target
	const TFLOAT fWidth  = g_pRender->GetSurfaceWidth();
	const TFLOAT fHeight = g_pRender->GetSurfaceHeight();

	m_matProjection = {
		2.0f / fWidth, 0.0f, 0.0f, 0.0f,
		0.0f, -2.0f / fHeight, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		-1.0f, 1.0f, 0.0f, 1.0f
	};

	// We render mid-GUI-pass and clobber shared state (b0 UI projection, blend,
	// viewport); save the viewport and restore game UI state in EndFrame so the
	// cursor and any later GUI draw correctly
	UINT uiNumViewports = 1;
	g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiNumViewports, &m_oSavedViewport );

	D3D11_VIEWPORT oViewport = { 0.0f, 0.0f, fWidth, fHeight, 0.0f, 1.0f };
	g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oViewport );

	g_pRender->SetCullMode( D3D11_CULL_NONE );
	g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA );
	g_pRender->SetZMode( TFALSE, D3D11_COMPARISON_ALWAYS, D3D11_DEPTH_WRITE_MASK_ZERO );
	g_pRender->SetAlphaToCoverageEnabled( TFALSE );
	g_pRender->PSSetSamplerState( 0, SAMPLER_LINEAR_CLAMP );
}

void remaster::RmlRenderInterfaceDX11::EndFrame()
{
	g_pRender->SetScissorEnabled( TFALSE );
	g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &m_oSavedViewport );

	// Re-establish the game UI render state (projection, blend, pipeline) we
	// overwrote, so the cursor drawn after this callback is not affected
	if ( g_pUIRender )
		g_pUIRender->PrepareRenderer();
}

Rml::CompiledGeometryHandle remaster::RmlRenderInterfaceDX11::CompileGeometry( Rml::Span<const Rml::Vertex> a_Vertices, Rml::Span<const int> a_Indices )
{
	const TUINT uiNumVertices = TUINT( a_Vertices.size() );
	const TUINT uiNumIndices  = TUINT( a_Indices.size() );

	auto pVertices = new RmlEngineVertex[ uiNumVertices ];
	for ( TUINT i = 0; i < uiNumVertices; i++ )
	{
		const Rml::Vertex& rSrc = a_Vertices[ i ];
		RmlEngineVertex&   rDst = pVertices[ i ];

		rDst.x = rSrc.position.x;
		rDst.y = rSrc.position.y;
		rDst.z = 0.0f;
		// Rml colour is premultiplied RGBA in memory; pack as ARGB so the B8G8R8A8 layout reads it back correctly
		rDst.uiColour = ( TUINT32( rSrc.colour.alpha ) << 24 ) | ( TUINT32( rSrc.colour.red ) << 16 ) | ( TUINT32( rSrc.colour.green ) << 8 ) | TUINT32( rSrc.colour.blue );
		rDst.u        = rSrc.tex_coord.x;
		rDst.v        = rSrc.tex_coord.y;
	}

	auto pGeometry           = new RmlGeometry;
	pGeometry->uiNumIndices  = uiNumIndices;
	pGeometry->pVertexBuffer = TNULL;
	pGeometry->pIndexBuffer  = TNULL;

	D3D11_BUFFER_DESC oVBDesc = {};
	oVBDesc.Usage             = D3D11_USAGE_IMMUTABLE;
	oVBDesc.ByteWidth         = uiNumVertices * sizeof( RmlEngineVertex );
	oVBDesc.BindFlags         = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA oVBData = {};
	oVBData.pSysMem                = pVertices;
	g_pRender->GetD3D11Device()->CreateBuffer( &oVBDesc, &oVBData, &pGeometry->pVertexBuffer );

	D3D11_BUFFER_DESC oIBDesc = {};
	oIBDesc.Usage             = D3D11_USAGE_IMMUTABLE;
	oIBDesc.ByteWidth         = uiNumIndices * sizeof( int );
	oIBDesc.BindFlags         = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA oIBData = {};
	oIBData.pSysMem                = a_Indices.data();
	g_pRender->GetD3D11Device()->CreateBuffer( &oIBDesc, &oIBData, &pGeometry->pIndexBuffer );

	delete[] pVertices;

	return Rml::CompiledGeometryHandle( pGeometry );
}

void remaster::RmlRenderInterfaceDX11::RenderGeometry( Rml::CompiledGeometryHandle a_Geometry, Rml::Vector2f a_vTranslation, Rml::TextureHandle a_Texture )
{
	auto pGeometry = TREINTERPRETCAST( RmlGeometry*, a_Geometry );

	const TMatrix44 matTranslate = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		a_vTranslation.x, a_vTranslation.y, 0.0f, 1.0f
	};

	// Shader does pos * ui_view * ui_projection. Translation applies in local
	// space, then the RmlUi element transform, so ui_view = translate * transform
	TMatrix44 matView;
	if ( m_bHasTransform )
		matView.Multiply( m_matTransform, matTranslate );
	else
		matView = matTranslate;

	g_pRender->VSBufferSetMat4( 0, m_matProjection );
	g_pRender->VSBufferSetMat4( 4, matView );

	auto pTextureSRV = TREINTERPRETCAST( ID3D11ShaderResourceView*, a_Texture );

	TUINT uiCombo = 0;
	if ( pTextureSRV )
		uiCombo = IsFontTexture( pTextureSRV ) ? shadercombos::UI_FONT : shadercombos::UI_TEXTURED;

	g_pRender->SetShaderPipelineState( m_vecPipelines[ shadercombos::GetUIComboIndex( uiCombo ) ] );
	g_pRender->PSSetShaderResource( 0, pTextureSRV );

	g_pRender->DrawIndexed(
	    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	    pGeometry->uiNumIndices,
	    pGeometry->pIndexBuffer, 0, DXGI_FORMAT_R32_UINT,
	    pGeometry->pVertexBuffer, sizeof( RmlEngineVertex ), 0,
	    TNULL
	);
}

void remaster::RmlRenderInterfaceDX11::ReleaseGeometry( Rml::CompiledGeometryHandle a_Geometry )
{
	auto pGeometry = TREINTERPRETCAST( RmlGeometry*, a_Geometry );
	if ( pGeometry->pVertexBuffer ) pGeometry->pVertexBuffer->Release();
	if ( pGeometry->pIndexBuffer ) pGeometry->pIndexBuffer->Release();
	delete pGeometry;
}

Rml::TextureHandle remaster::RmlRenderInterfaceDX11::CreateTextureRGBA( const void* a_pData, TINT a_iWidth, TINT a_iHeight )
{
	D3D11_TEXTURE2D_DESC oDesc = {};
	oDesc.Width                = a_iWidth;
	oDesc.Height               = a_iHeight;
	oDesc.MipLevels            = 1;
	oDesc.ArraySize            = 1;
	oDesc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
	oDesc.SampleDesc.Count     = 1;
	oDesc.Usage                = D3D11_USAGE_IMMUTABLE;
	oDesc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA oData = {};
	oData.pSysMem                = a_pData;
	oData.SysMemPitch            = a_iWidth * 4;

	ID3D11Texture2D* pTexture = TNULL;
	if ( FAILED( g_pRender->GetD3D11Device()->CreateTexture2D( &oDesc, &oData, &pTexture ) ) )
		return 0;

	ID3D11ShaderResourceView* pSRV = TNULL;
	const HRESULT             hr   = g_pRender->GetD3D11Device()->CreateShaderResourceView( pTexture, TNULL, &pSRV );
	pTexture->Release();

	if ( FAILED( hr ) )
		return 0;

	return Rml::TextureHandle( pSRV );
}

// Rasterises an SVG into a premultiplied-RGBA buffer. The texture is supersampled so
// it stays crisp when scaled up, while a_rIntrinsic reports the authored size as the
// element's natural dimensions
static TBOOL RasterizeSVG( const Rml::String& a_rSource, Rml::Vector<Rml::byte>& a_rPixels, TINT& a_riTexW, TINT& a_riTexH, Rml::Vector2i& a_rIntrinsic )
{
	constexpr TFLOAT SUPERSAMPLE = 2.0f;
	constexpr TINT   MAX_DIM     = 1024;

	NSVGimage* pImage = nsvgParseFromFile( a_rSource.c_str(), "px", 96.0f );
	if ( !pImage )
		return TFALSE;

	if ( pImage->width <= 0.0f || pImage->height <= 0.0f )
	{
		nsvgDelete( pImage );
		return TFALSE;
	}

	TFLOAT flScale = SUPERSAMPLE;
	if ( pImage->width * flScale > MAX_DIM || pImage->height * flScale > MAX_DIM )
		flScale = MAX_DIM / TMath::Max( pImage->width, pImage->height );

	a_riTexW = TINT( pImage->width * flScale + 0.5f );
	a_riTexH = TINT( pImage->height * flScale + 0.5f );
	a_rIntrinsic.x = TINT( pImage->width + 0.5f );
	a_rIntrinsic.y = TINT( pImage->height + 0.5f );

	NSVGrasterizer* pRast = nsvgCreateRasterizer();
	a_rPixels.assign( TSIZE( a_riTexW ) * a_riTexH * 4, Rml::byte( 0 ) );
	nsvgRasterize( pRast, pImage, 0.0f, 0.0f, flScale, a_rPixels.data(), a_riTexW, a_riTexH, a_riTexW * 4 );
	nsvgDeleteRasterizer( pRast );
	nsvgDelete( pImage );

	// nanosvg emits straight-alpha RGBA; Rml expects premultiplied
	for ( TSIZE i = 0; i + 3 < a_rPixels.size(); i += 4 )
	{
		const TUINT uiA    = a_rPixels[ i + 3 ];
		a_rPixels[ i + 0 ] = Rml::byte( a_rPixels[ i + 0 ] * uiA / 255 );
		a_rPixels[ i + 1 ] = Rml::byte( a_rPixels[ i + 1 ] * uiA / 255 );
		a_rPixels[ i + 2 ] = Rml::byte( a_rPixels[ i + 2 ] * uiA / 255 );
	}

	return TTRUE;
}

Rml::TextureHandle remaster::RmlRenderInterfaceDX11::LoadTexture( Rml::Vector2i& a_rDimensions, const Rml::String& a_rSource )
{
	// SVG sources rasterise through nanosvg; everything else goes through stb_image
	if ( a_rSource.size() >= 4 && Rml::StringUtilities::ToLower( a_rSource.substr( a_rSource.size() - 4 ) ) == ".svg" )
	{
		Rml::Vector<Rml::byte> vecPixels;
		TINT                   iTexW = 0, iTexH = 0;
		if ( !RasterizeSVG( a_rSource, vecPixels, iTexW, iTexH, a_rDimensions ) )
			return 0;

		return CreateTextureRGBA( vecPixels.data(), iTexW, iTexH );
	}

	TINT iWidth, iHeight, iChannels;
	stbi_uc* pPixels = stbi_load( a_rSource.c_str(), &iWidth, &iHeight, &iChannels, 4 );
	if ( !pPixels )
		return 0;

	// Rml expects premultiplied alpha
	for ( TINT i = 0; i < iWidth * iHeight; i++ )
	{
		stbi_uc* p       = pPixels + i * 4;
		const TUINT uiA  = p[ 3 ];
		p[ 0 ]           = stbi_uc( p[ 0 ] * uiA / 255 );
		p[ 1 ]           = stbi_uc( p[ 1 ] * uiA / 255 );
		p[ 2 ]           = stbi_uc( p[ 2 ] * uiA / 255 );
	}

	a_rDimensions.x = iWidth;
	a_rDimensions.y = iHeight;

	const Rml::TextureHandle hTexture = CreateTextureRGBA( pPixels, iWidth, iHeight );
	stbi_image_free( pPixels );
	return hTexture;
}

Rml::TextureHandle remaster::RmlRenderInterfaceDX11::GenerateTexture( Rml::Span<const Rml::byte> a_Source, Rml::Vector2i a_Dimensions )
{
	return CreateTextureRGBA( a_Source.data(), a_Dimensions.x, a_Dimensions.y );
}

void remaster::RmlRenderInterfaceDX11::ReleaseTexture( Rml::TextureHandle a_Texture )
{
	if ( a_Texture )
		TREINTERPRETCAST( ID3D11ShaderResourceView*, a_Texture )->Release();
}

void remaster::RmlRenderInterfaceDX11::EnableScissorRegion( bool a_bEnable )
{
	g_pRender->SetScissorEnabled( a_bEnable );
}

void remaster::RmlRenderInterfaceDX11::SetScissorRegion( Rml::Rectanglei a_Region )
{
	D3D11_RECT oRect;
	oRect.left   = a_Region.Left();
	oRect.top    = a_Region.Top();
	oRect.right  = a_Region.Right();
	oRect.bottom = a_Region.Bottom();
	g_pRender->GetD3D11DeviceContext()->RSSetScissorRects( 1, &oRect );
}

void remaster::RmlRenderInterfaceDX11::SetTransform( const Rml::Matrix4f* a_pTransform )
{
	m_bHasTransform = ( a_pTransform != TNULL );
	if ( !m_bHasTransform )
		return;

	// Rml Matrix4f is column-major; loading its columns into the row-major
	// TMatrix44 stores the transpose, which is what the row-vector shader wants
	const float* p = a_pTransform->data();
	m_matTransform = {
		p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ],
		p[ 4 ], p[ 5 ], p[ 6 ], p[ 7 ],
		p[ 8 ], p[ 9 ], p[ 10 ], p[ 11 ],
		p[ 12 ], p[ 13 ], p[ 14 ], p[ 15 ]
	};
}
