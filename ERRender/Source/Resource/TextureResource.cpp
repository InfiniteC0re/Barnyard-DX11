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

static TBOOL ImageHasTransparency( const DirectX::ScratchImage& a_rcImage, const DirectX::TexMetadata& a_rcMeta )
{
	const DXGI_FORMAT eFmt = a_rcMeta.format;
	if ( !DirectX::HasAlpha( eFmt ) )
		return TFALSE;

	const DirectX::Image* pImg = a_rcImage.GetImage( 0, 0, 0 );
	if ( !pImg || !pImg->pixels )
		return TFALSE;

	if ( eFmt == DXGI_FORMAT_BC1_UNORM || eFmt == DXGI_FORMAT_BC1_UNORM_SRGB )
	{
		const TSIZE uiNumBlocks = pImg->slicePitch / 8;
		for ( TSIZE b = 0; b < uiNumBlocks; b++ )
		{
			const TUINT8* pBlock = pImg->pixels + b * 8;
			const TUINT16 uiC0   = TUINT16( pBlock[ 0 ] | ( pBlock[ 1 ] << 8 ) );
			const TUINT16 uiC1   = TUINT16( pBlock[ 2 ] | ( pBlock[ 3 ] << 8 ) );
			if ( uiC0 > uiC1 )
				continue;
			TUINT32 uiIndices;
			TUtil::MemCopy( &uiIndices, pBlock + 4, 4 );
			// any 2-bit index == 3 selects the transparent entry
			if ( ( uiIndices & ( uiIndices >> 1 ) & 0x55555555u ) != 0 )
				return TTRUE;
		}
		return TFALSE;
	}

	if ( eFmt == DXGI_FORMAT_BC2_UNORM || eFmt == DXGI_FORMAT_BC2_UNORM_SRGB )
	{
		const TSIZE uiNumBlocks = pImg->slicePitch / 16;
		for ( TSIZE b = 0; b < uiNumBlocks; b++ )
		{
			const TUINT8* pBlock = pImg->pixels + b * 16;
			for ( TINT i = 0; i < 8; i++ )
			{
				if ( pBlock[ i ] != 0xFF )
					return TTRUE;
			}
		}
		return TFALSE;
	}

	if ( eFmt == DXGI_FORMAT_BC3_UNORM || eFmt == DXGI_FORMAT_BC3_UNORM_SRGB )
	{
		const TSIZE uiNumBlocks = pImg->slicePitch / 16;
		for ( TSIZE b = 0; b < uiNumBlocks; b++ )
		{
			const TUINT8* pBlock = pImg->pixels + b * 16;
			if ( pBlock[ 0 ] != 0xFF || pBlock[ 1 ] != 0xFF )
				return TTRUE;
		}
		return TFALSE;
	}

	// Uncompressed formats: exact per-pixel scan
	return !a_rcImage.IsAlphaAllOpaque();
}

TBOOL remaster::TextureResource_HasTransparency( Toshi::TTexture* a_pTexture )
{
	return a_pTexture && ( a_pTexture->GetTextureFlags() & TEXTUREFLAG_HAS_TRANSPARENCY ) != 0;
}

TBOOL remaster::TextureResource_IsOpaque( Toshi::TTexture* a_pTexture )
{
	if ( !a_pTexture )
		return TFALSE;

	const TUINT uiFlags = a_pTexture->GetTextureFlags();
	return ( uiFlags & ( TEXTUREFLAG_ALPHA_SCANNED | TEXTUREFLAG_HAS_TRANSPARENCY ) ) == TEXTUREFLAG_ALPHA_SCANNED;
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

		m_ImageInfo.Depth = ImageHasTransparency( scratchImage, texMetadata ) ? 2 : 1;

		*(ID3D11ShaderResourceView**)( &m_pD3DTexture ) = pSRV;
	}

	return 0;
}

MEMBER_HOOK( 0x006c0cc0, Toshi::TTextureResourceHAL, TTextureResourceHAL_CreateFromT2Texture, void, Toshi::T2Texture* a_pT2Texture )
{
	CallOriginal( a_pT2Texture );

	m_eTextureFlags &= ~remaster::TEXTUREFLAG_HAS_TRANSPARENCY;
	m_eTextureFlags |= remaster::TEXTUREFLAG_ALPHA_SCANNED;

	if ( m_ImageInfo.Depth == 2 )
		m_eTextureFlags |= remaster::TEXTUREFLAG_HAS_TRANSPARENCY;

	m_ImageInfo.Depth = 1;
}

void remaster::SetupRenderHooks_TextureResource()
{
	InstallHook<T2Texture_Load>();
	InstallHook<TTextureResourceHAL_CreateFromT2Texture>();
	InstallHook<TTextureResourceHAL_CreateFromMemory4444>();
	InstallHook<TTextureResourceHAL_CreateFromMemory8888>();
}
