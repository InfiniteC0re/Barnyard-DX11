#include "pch.h"
#include "TextureResource.h"
#include "RenderDX11.h"
#include "RenderDX11Utils.h"

#include "SOIL2/stb_image.h"

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

MEMBER_HOOK( 0x00615bc0, Toshi::T2Texture, T2Texture_Load, HRESULT )
{
	TPROFILER_SCOPE();
	TASSERT( m_pData != TNULL && m_uiDataSize != 0 );

	HRESULT hRes = D3DXGetImageInfoFromFileInMemory( m_pData, m_uiDataSize, &m_ImageInfo );

	TINT   iWidth, iHeight, iChannels;
	TBYTE* pTexData = stbi_load_from_memory( (TBYTE*)m_pData, m_uiDataSize, &iWidth, &iHeight, &iChannels, 4 );

	// Create D3D11 texture and write it to the structure
	// We DON'T need to hook AMaterialLibrary::DestroyTextures, because VTable matches fine for releasing objects

	*(ID3D11ShaderResourceView**)( &m_pD3DTexture ) = remaster::dx11::CreateTexture(
	    m_ImageInfo.Width,
	    m_ImageInfo.Height,
	    DXGI_FORMAT_R8G8B8A8_UNORM,
	    pTexData,
	    D3D11_USAGE_DEFAULT,
	    D3D11_CPU_ACCESS_WRITE,
	    1,
		remaster::dx11::CTF_GEN_MIPMAPS
	);

	stbi_image_free( pTexData );

	return 0;
}

void remaster::SetupRenderHooks_TextureResource()
{
	InstallHook<T2Texture_Load>();
	InstallHook<TTextureResourceHAL_CreateFromMemory4444>();
}
