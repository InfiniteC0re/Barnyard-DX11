#include "pch.h"
#include "RenderDX11Utils.h"
#include "RenderDX11.h"

#include <d3dcompiler.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

ID3DBlob* remaster::dx11::CompileShader( const TCHAR* a_pchSrcData, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const D3D_SHADER_MACRO* a_pDefines )
{
	TSIZE srcLength = T2String8::Length( a_pchSrcData );

	ID3DBlob* pShaderBlob = TNULL;
	ID3DBlob* pErrorBlob  = TNULL;

	HRESULT hRes = D3DCompile( a_pchSrcData, srcLength, NULL, a_pDefines, NULL, a_pEntrypoint, a_pTarget, D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, 0, &pShaderBlob, &pErrorBlob );

	if ( !SUCCEEDED( hRes ) )
	{
		TERROR( "Shader compilation failed\n" );

		if ( pErrorBlob != TNULL )
		{
			TERROR( (const TCHAR*)pErrorBlob->GetBufferPointer() );
			pErrorBlob->Release();
		}

		TASSERT( TFALSE );
	}

	return pShaderBlob;
}

ID3DBlob* remaster::dx11::CompileShaderFromFile( const TCHAR* a_pchFilepath, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const D3D_SHADER_MACRO* a_pDefines )
{
	TFile* pFile    = TFile::Create( a_pchFilepath );
	DWORD  fileSize = pFile->GetSize();
	TCHAR* srcData  = new TCHAR[ fileSize + 1 ];
	pFile->Read( srcData, fileSize );
	srcData[ fileSize ] = '\0';
	pFile->Destroy();

	ID3DBlob* shader = CompileShader( srcData, a_pEntrypoint, a_pTarget, a_pDefines );
	delete[] srcData;

	return shader;
}

HRESULT remaster::dx11::CreatePixelShader( const void* a_pShaderBytecode, SIZE_T a_uiBytecodeLength, ID3D11PixelShader** a_ppPixelShader )
{
	HRESULT hRes = g_pRender->GetD3D11Device()->CreatePixelShader( a_pShaderBytecode, a_uiBytecodeLength, NULL, a_ppPixelShader );
	TASSERT( SUCCEEDED( hRes ), "Couldnt Create Pixel Shader" );

	return hRes;
}

HRESULT remaster::dx11::CreateVertexShader( const void* a_pShaderBytecode, SIZE_T a_uiBytecodeLength, ID3D11VertexShader** a_ppVertexShader )
{
	HRESULT hRes = g_pRender->GetD3D11Device()->CreateVertexShader( a_pShaderBytecode, a_uiBytecodeLength, NULL, a_ppVertexShader );
	TASSERT( SUCCEEDED( hRes ), "Couldnt Create Vertex Shader" );

	return hRes;
}

ID3D11Buffer* remaster::dx11::CreateBuffer( TUINT a_uiFlags, TUINT a_uiDataSize, const void* a_pData, D3D11_USAGE a_eUsage, TUINT a_eCPUAccessFlags )
{
	D3D11_BUFFER_DESC bufferDesc;

	bufferDesc.ByteWidth           = a_uiDataSize;
	bufferDesc.Usage               = a_eUsage;
	bufferDesc.CPUAccessFlags      = a_eCPUAccessFlags;
	bufferDesc.MiscFlags           = 0;
	bufferDesc.StructureByteStride = 0;

	if ( a_uiFlags == 0 )
	{
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	}
	else
	{
		bufferDesc.BindFlags = ( a_uiFlags == 1 ) ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_CONSTANT_BUFFER;
	}

	ID3D11Buffer* pBuffer;

	if ( a_pData != TNULL )
	{
		D3D11_SUBRESOURCE_DATA subData;
		subData.pSysMem          = a_pData;
		subData.SysMemPitch      = 0;
		subData.SysMemSlicePitch = 0;

		g_pRender->GetD3D11Device()->CreateBuffer( &bufferDesc, &subData, &pBuffer );
	}
	else
	{
		g_pRender->GetD3D11Device()->CreateBuffer( &bufferDesc, TNULL, &pBuffer );
	}

	return pBuffer;
}

ID3D11ShaderResourceView* remaster::dx11::CreateTexture( TUINT a_uiWidth, TUINT a_uiHeight, DXGI_FORMAT a_eFormat, const void* a_pData, D3D11_USAGE a_eUsage, TUINT32 a_eCPUAccessFlags, TUINT32 a_uiSampleDescCount, CreateTextureFlags a_eFlags )
{
	D3D11_SUBRESOURCE_DATA subResourceData = {};
	D3D11_TEXTURE2D_DESC   textureDesc     = {};

	textureDesc.SampleDesc.Count   = a_uiSampleDescCount;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.ArraySize          = 1;
	textureDesc.Usage              = a_eUsage;
	textureDesc.Width              = a_uiWidth;
	textureDesc.Height             = a_uiHeight;
	textureDesc.Format             = a_eFormat;
	textureDesc.CPUAccessFlags     = a_eCPUAccessFlags;
	textureDesc.MipLevels          = 1;
	textureDesc.MiscFlags          = false ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	textureDesc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;

	if ( a_eFlags & CTF_RENDER_TARGET )
		textureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D* pTexture = TNULL;

	if ( a_pData == TNULL )
	{
		g_pRender->GetD3D11Device()->CreateTexture2D( &textureDesc, TNULL, &pTexture );
	}
	else
	{
		D3D11_SUBRESOURCE_DATA subresourceData;
		subresourceData.pSysMem          = a_pData;
		subresourceData.SysMemPitch      = GetTextureRowPitch( a_eFormat, a_uiWidth );
		subresourceData.SysMemSlicePitch = GetTextureDepthPitch( a_eFormat, a_uiWidth, a_uiHeight );

		DX11_API_VALIDATE( g_pRender->GetD3D11Device()->CreateTexture2D( &textureDesc, &subresourceData, &pTexture ) );
	}

	if ( pTexture )
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
		shaderResourceViewDesc.Format                    = textureDesc.Format;
		shaderResourceViewDesc.ViewDimension             = ( a_uiSampleDescCount > 1 ) ? D3D_SRV_DIMENSION_TEXTURE2DMS : D3D_SRV_DIMENSION_TEXTURE2D;
		shaderResourceViewDesc.Texture2D.MipLevels       = 1;
		shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;

		ID3D11ShaderResourceView* pShaderResourceView = TNULL;
		DX11_API_VALIDATE( g_pRender->GetD3D11Device()->CreateShaderResourceView( pTexture, &shaderResourceViewDesc, &pShaderResourceView ) );

		if ( false && pShaderResourceView != TNULL )
		{
			g_pRender->GetD3D11DeviceContext()->GenerateMips( pShaderResourceView );
		}

		pTexture->Release();
		return pShaderResourceView;
	}

	return TNULL;
}

TINT remaster::dx11::GetTextureRowPitch( DXGI_FORMAT a_eFormat, TUINT a_uiWidth )
{
	switch ( a_eFormat )
	{
		case DXGI_FORMAT_UNKNOWN: return 0;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return a_uiWidth << 4;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return a_uiWidth << 3;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_B8G8R8A8_UNORM: return a_uiWidth << 2;
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_R8_UINT: return a_uiWidth;
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC4_UNORM: return ( ( a_uiWidth + 3U ) >> 2 ) << 3;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC5_UNORM: return ( ( a_uiWidth + 3U ) >> 2 ) << 4;
		case DXGI_FORMAT_B8G8R8X8_UNORM: return a_uiWidth * 3;
		case DXGI_FORMAT_B4G4R4A4_UNORM: return a_uiWidth * 2;
	}

	TASSERT( TFALSE );
	return 0;
}

TINT remaster::dx11::GetTextureDepthPitch( DXGI_FORMAT a_eFormat, TUINT a_uiWidth, TUINT a_uiHeight )
{
	switch ( a_eFormat )
	{
		case DXGI_FORMAT_UNKNOWN: return 0;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return a_uiWidth * a_uiHeight * 16;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return a_uiWidth * a_uiHeight * 8;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_B8G8R8A8_UNORM: return a_uiWidth * a_uiHeight * 4;
		case DXGI_FORMAT_R8_UINT:;
		case DXGI_FORMAT_A8_UNORM: return a_uiWidth * a_uiHeight;
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC4_UNORM: return ( ( a_uiWidth + 3U ) >> 2 ) * ( ( a_uiHeight + 3U ) >> 2 ) * 8;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC5_UNORM: return ( ( a_uiWidth + 3U ) >> 2 ) * ( ( a_uiHeight + 3U ) >> 2 ) * 16;
		case DXGI_FORMAT_B8G8R8X8_UNORM: return a_uiWidth * a_uiHeight * 3;
		case DXGI_FORMAT_B4G4R4A4_UNORM: return a_uiWidth * a_uiHeight * 2;
	}

	TASSERT( TFALSE );
	return 0;
}

TBOOL remaster::dx11::IsColorEqual( const TFLOAT a_pColor1[ 4 ], const TFLOAT a_pColor2[ 4 ] )
{
	return ( a_pColor1[ 0 ] == a_pColor2[ 0 ] ) && ( a_pColor1[ 1 ] == a_pColor2[ 1 ] ) && ( a_pColor1[ 2 ] == a_pColor2[ 2 ] ) && ( a_pColor1[ 3 ] == a_pColor2[ 3 ] );
}
