#include "pch.h"
#include "SysShader.h"
#include "SysMaterial.h"

#include <AHooks.h>
#include <HookHelpers.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x005f04e0, ASysShaderHAL, ASysShaderHAL_Constructor, remaster::SysShaderDX11* )
{
	return new remaster::SysShaderDX11();
}

void remaster::SetupRenderHooks_SysShader()
{
	InstallHook<ASysShaderHAL_Constructor>();
}

remaster::SysShaderDX11::SysShaderDX11()
{
	// Set Singleton
	*(ASysShader**)( 0x0079a340 ) = this;
}

remaster::SysShaderDX11::~SysShaderDX11()
{
}

void remaster::SysShaderDX11::Flush()
{
}

void remaster::SysShaderDX11::StartFlush()
{
}

void remaster::SysShaderDX11::EndFlush()
{
}

TBOOL remaster::SysShaderDX11::Create()
{
	return TFALSE;
}

TBOOL remaster::SysShaderDX11::Validate()
{
	return TFALSE;
}

void remaster::SysShaderDX11::Invalidate()
{
}

TBOOL remaster::SysShaderDX11::TryInvalidate()
{
	return TFALSE;
}

TBOOL remaster::SysShaderDX11::TryValidate()
{
	return TFALSE;
}

void remaster::SysShaderDX11::Render( Toshi::TRenderPacket* pPacket )
{
}

ASysMaterial* remaster::SysShaderDX11::CreateMaterial( const TCHAR* a_szName )
{
	Validate();

	auto pMaterialHAL = new SysMaterial();
	pMaterialHAL->SetShader( this );
	pMaterialHAL->SetOrderTable( GetOrderTable( 0 ) );

	return pMaterialHAL;
}

ASysMesh* remaster::SysShaderDX11::CreateMesh( const TCHAR* a_szName )
{
	return TNULL;
}
