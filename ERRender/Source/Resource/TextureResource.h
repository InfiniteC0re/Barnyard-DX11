#pragma once
#include <Platform/DX8/TTextureResourceHAL_DX8.h>

namespace remaster
{

// Custom TTexture::m_eTextureFlags flags
inline constexpr TUINT TEXTUREFLAG_ALPHA_SCANNED    = 0x80;
inline constexpr TUINT TEXTUREFLAG_HAS_TRANSPARENCY = 0x100;

void SetupRenderHooks_TextureResource();

TBOOL TextureResource_HasTransparency( Toshi::TTexture* a_pTexture );

TBOOL TextureResource_IsOpaque( Toshi::TTexture* a_pTexture );

}; // namespace remaster
