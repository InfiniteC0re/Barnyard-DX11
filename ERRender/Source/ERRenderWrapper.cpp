#include "pch.h"
#include "ERRender.h"
#include "Shader/GrassShader.h"
#include "Shader/SkinShader.h"
#include "Shader/WorldShader.h"
#include "Shader/StaticInstanceShader.h"
#include "Shader/SysShader.h"
#include "Resource/TextureResource.h"
#include "Resource/Viewport.h"
#include "Resource/OrderTable.h"
#include "Resource/ClassPatcher.h"
#include "UI/GUI2Renderer.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Platform/DX8/TRenderInterface_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

HOOK( 0x006c6d60, TRenderD3DInterface_CreateObject, TRenderInterface* )
{
	return new remaster::RenderDX11;
}

MEMBER_HOOK( 0x006c72a0, remaster::RenderDX11, TRenderD3DInterface_Create, TBOOL, const char* a_pchWindowTitle )
{
	return Create( a_pchWindowTitle );
}

MEMBER_HOOK( 0x006c58e0, remaster::RenderDX11, TRenderD3DInterface_BeginEndScene, void )
{
}

void remaster::SetupRenderHooks()
{
	InstallHook<TRenderD3DInterface_Create>();
	InstallHook<TRenderD3DInterface_CreateObject>();
	InstallHook<TRenderD3DInterface_BeginEndScene>();

	SetupRenderHooks_GrassShader();
	SetupRenderHooks_SkinShader();
	SetupRenderHooks_WorldShader();
	SetupRenderHooks_TextureResource();
	SetupRenderHooks_Viewport();
	SetupRenderHooks_StaticInstanceShader();
	SetupRenderHooks_SysShader();
	SetupRenderHooks_UIRenderer();
	SetupRenderHooks_OrderTable();
}
