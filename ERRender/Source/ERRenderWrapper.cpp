#include "pch.h"
#include "RenderDX11.h"
#include "Shader/GrassShader.h"
#include "Shader/SkinShader.h"
#include "Shader/WorldShader.h"
#include "Shader/StaticInstanceShader.h"
#include "Shader/SysShader.h"
#include "Resource/TextureResource.h"
#include "Resource/Viewport.h"
#include "Resource/OrderTable.h"
#include "Resource/ClassPatcher.h"
#include "Resource/VertexBlock.h"
#include "Resource/IndexBlock.h"
#include "UI/UIRenderer.h"
#include "UI/FontRenderer.h"

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
	return Create( "Barnyard Remastered" );
}

MEMBER_HOOK( 0x006c58e0, remaster::RenderDX11, TRenderD3DInterface_BeginEndScene, void )
{
}

MEMBER_HOOK( 0x006be990, remaster::RenderDX11, TRenderD3DInterface_FlushShaders, void )
{
	FlushOrderTables();

	for ( auto it = TShader::sm_oShaderList.GetRootShader(); it != TNULL; it = it->GetNextShader() )
	{
		it->Flush();
	}
}

void remaster::SetupRenderHooks()
{
	InstallHook<TRenderD3DInterface_Create>();
	InstallHook<TRenderD3DInterface_CreateObject>();
	InstallHook<TRenderD3DInterface_BeginEndScene>();
	InstallHook<TRenderD3DInterface_FlushShaders>();

	SetupRenderHooks_GrassShader();
	SetupRenderHooks_SkinShader();
	SetupRenderHooks_WorldShader();
	SetupRenderHooks_TextureResource();
	SetupRenderHooks_Viewport();
	SetupRenderHooks_StaticInstanceShader();
	SetupRenderHooks_SysShader();
	SetupRenderHooks_UIRenderer();
	SetupRenderHooks_FontRenderer();
	SetupRenderHooks_OrderTable();
	SetupRenderHooks_VertexBlock();
	SetupRenderHooks_IndexBlock();
}
