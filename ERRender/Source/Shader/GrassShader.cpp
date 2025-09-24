#include "pch.h"
#include "GrassShader.h"
#include "GrassMesh.h"
#include "Resource/ClassPatcher.h"

#include <AHooks.h>
#include <HookHelpers.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::GrassShaderDX11, 0x0079aacc );

MEMBER_HOOK( 0x005f7c10, AGrassShaderHAL, AGrassShaderHAL_Constructor, remaster::GrassShaderDX11* )
{
	TFree( this );
	return new remaster::GrassShaderDX11();
}

void remaster::SetupRenderHooks_GrassShader()
{
	InstallHook<AGrassShaderHAL_Constructor>();
}

remaster::GrassShaderDX11::GrassShaderDX11()
{
	// Set Singleton
	*(AGrassShader**)( 0x0079aa24 ) = this;
}

remaster::GrassShaderDX11::~GrassShaderDX11()
{
}

void remaster::GrassShaderDX11::Flush()
{
}

void remaster::GrassShaderDX11::StartFlush()
{
}

void remaster::GrassShaderDX11::EndFlush()
{
}

TBOOL remaster::GrassShaderDX11::Create()
{
	return TFALSE;
}

TBOOL remaster::GrassShaderDX11::Validate()
{
	return TFALSE;
}

void remaster::GrassShaderDX11::Invalidate()
{
}

TBOOL remaster::GrassShaderDX11::TryInvalidate()
{
	return TFALSE;
}

TBOOL remaster::GrassShaderDX11::TryValidate()
{
	return TFALSE;
}

void remaster::GrassShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
}

AGrassMaterial* remaster::GrassShaderDX11::CreateMaterial( const TCHAR* a_szName )
{

	return TNULL;
}

AGrassMesh* remaster::GrassShaderDX11::CreateMesh( const TCHAR* a_szName )
{
	Validate();

	AGrassMeshHAL* pMesh = new AGrassMeshHAL();
	pMesh->SetOwnerShader( this );

	return pMesh;
}
