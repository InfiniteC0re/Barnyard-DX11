#include "pch.h"
#include "TextureResource.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Platform/DX8/T2Texture_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x006c13f0, Toshi::TTextureResourceHAL, TTextureResourceHAL_Validate, TBOOL )
{
	return TTRUE;
}

MEMBER_HOOK( 0x00615bc0, Toshi::T2Texture, T2Texture_Load, HRESULT )
{
	return 0;
}

MEMBER_HOOK( 0x00615f60, AMaterialLibrary, AMaterialLibrary_DestroyTextures, void )
{
	return;
}

void remaster::SetupRenderHooks_TextureResource()
{
	InstallHook<TTextureResourceHAL_Validate>();
	InstallHook<T2Texture_Load>();
	InstallHook<AMaterialLibrary_DestroyTextures>();
}
