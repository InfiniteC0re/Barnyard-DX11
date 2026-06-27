#include "pch.h"
#include "TextureResource.h"
#include "RenderDX11.h"
#include "RenderDX11Utils.h"

#include "DirectXTex/DirectXTex.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Platform/DX8/T2Texture_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x006c0ef0, Toshi::TTextureResourceHAL, TTextureResourceHAL_CreateFromMemory4444, TBOOL, TUINT a_uiWidth, TUINT a_uiHeight, TUINT a_uiLevels, void* a_pData )
{
	TPROFILER_SCOPE();

	ID3D11ShaderResourceView* pTexture = remaster::dx11::CreateTexture(
	    a_uiWidth,
	    a_uiHeight,
	    DXGI_FORMAT_B4G4R4A4_UNORM,
	    a_pData,
	    D3D11_USAGE_IMMUTABLE,
	    0,
	    1
	);

	TUtil::MemClear( &m_ImageInfo, sizeof( m_ImageInfo ) );
	m_ImageInfo.Width  = a_uiWidth;
	m_ImageInfo.Height = a_uiHeight;

	return pTexture;
}

MEMBER_HOOK( 0x006c0ff0, Toshi::TTextureResourceHAL, TTextureResourceHAL_CreateFromMemory8888, TBOOL, TUINT a_uiWidth, TUINT a_uiHeight, TUINT a_uiLevels, void* a_pData )
{
	TPROFILER_SCOPE();

	ID3D11ShaderResourceView* pTexture = remaster::dx11::CreateTexture(
	    a_uiWidth,
	    a_uiHeight,
	    DXGI_FORMAT_B8G8R8A8_UNORM,
	    a_pData,
	    D3D11_USAGE_IMMUTABLE,
	    0,
	    1
	);

	TUtil::MemClear( &m_ImageInfo, sizeof( m_ImageInfo ) );
	m_ImageInfo.Width  = a_uiWidth;
	m_ImageInfo.Height = a_uiHeight;

	return pTexture;
}

MEMBER_HOOK( 0x00615bc0, Toshi::T2Texture, T2Texture_Load, HRESULT )
{
	TPROFILER_SCOPE();
	TASSERT( m_pData != TNULL && m_uiDataSize != 0 );

	// NOTE: m_ImageInfo must be here because the game reads it to assign blend states!!!
	D3DXGetImageInfoFromFileInMemory( m_pData, m_uiDataSize, &m_ImageInfo );

	// Create D3D11 texture and write it to the structure
	// We DON'T need to hook AMaterialLibrary::DestroyTextures, because VTable matches fine for releasing objects

	DirectX::ScratchImage scratchImage;
	DirectX::TexMetadata  texMetadata;
	HRESULT               hRes = E_FAIL;

	// DDS files may contain stored mipmaps, so load them as-is
	m_ImageInfo.ImageFileFormat = D3DXIFF_DDS;
	hRes = DirectX::LoadFromDDSMemory(
	    static_cast<const uint8_t*>( m_pData ),
	    m_uiDataSize,
	    DirectX::DDS_FLAGS_NONE,
	    &texMetadata,
	    scratchImage
	);

	if ( FAILED( hRes ) )
	{
		// Fall back to TGA
		m_ImageInfo.ImageFileFormat = D3DXIFF_TGA;
		hRes = DirectX::LoadFromTGAMemory(
		    static_cast<const uint8_t*>( m_pData ),
		    m_uiDataSize,
		    DirectX::TGA_FLAGS_NONE,
		    &texMetadata,
		    scratchImage
		);
	}

	if ( FAILED( hRes ) )
	{
		// Fall back to WIC (PNG, JPG, BMP, etc.)
		m_ImageInfo.ImageFileFormat = D3DXIFF_PNG;
		hRes = DirectX::LoadFromWICMemory(
		    static_cast<const uint8_t*>( m_pData ),
		    m_uiDataSize,
		    DirectX::WIC_FLAGS_NONE,
		    &texMetadata,
		    scratchImage
		);
	}

	TASSERT( SUCCEEDED( hRes ), "T2Texture_Load: Failed to load texture with DirectXTex" );

	if ( SUCCEEDED( hRes ) )
	{
		m_ImageInfo.Width     = TUINT( texMetadata.width );
		m_ImageInfo.Height    = TUINT( texMetadata.height );
		m_ImageInfo.MipLevels = TUINT( texMetadata.mipLevels );

		ID3D11ShaderResourceView* pSRV = TNULL;
		DirectX::CreateShaderResourceView(
		    remaster::g_pRender->GetD3D11Device(),
		    scratchImage.GetImages(),
		    scratchImage.GetImageCount(),
		    texMetadata,
		    &pSRV
		);

		*(ID3D11ShaderResourceView**)( &m_pD3DTexture ) = pSRV;
	}

	return 0;
}

void remaster::SetupRenderHooks_TextureResource()
{
	InstallHook<T2Texture_Load>();
	InstallHook<TTextureResourceHAL_CreateFromMemory4444>();
	InstallHook<TTextureResourceHAL_CreateFromMemory8888>();
}
