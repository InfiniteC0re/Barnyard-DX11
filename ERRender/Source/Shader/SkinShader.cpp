#include "pch.h"
#include "SkinShader.h"
#include "SkinMaterial.h"
#include "SkinMesh.h"
#include "Resource/ClassPatcher.h"

#include <AHooks.h>
#include <HookHelpers.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::SkinShaderDX11, 0x0079a648 );

MEMBER_HOOK( 0x005f46a0, ASkinShaderHAL, ASkinShaderHAL_Constructor, remaster::SkinShaderDX11* )
{
	return new remaster::SkinShaderDX11();
}

void remaster::SetupRenderHooks_SkinShader()
{
	InstallHook<ASkinShaderHAL_Constructor>();
}

remaster::SkinShaderDX11::SkinShaderDX11()
{
	// Set Singleton
	*(ASkinShader**)( 0x0079a4f8 ) = this;
}

remaster::SkinShaderDX11::~SkinShaderDX11()
{
}

void remaster::SkinShaderDX11::Flush()
{
}

void remaster::SkinShaderDX11::StartFlush()
{
}

void remaster::SkinShaderDX11::EndFlush()
{
}

TBOOL remaster::SkinShaderDX11::Create()
{
	return TFALSE;
}

TBOOL remaster::SkinShaderDX11::Validate()
{
	return TFALSE;
}

void remaster::SkinShaderDX11::Invalidate()
{
}

TBOOL remaster::SkinShaderDX11::TryInvalidate()
{
	return TFALSE;
}

TBOOL remaster::SkinShaderDX11::TryValidate()
{
	return TFALSE;
}

void remaster::SkinShaderDX11::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
}

void remaster::SkinShaderDX11::EnableRenderEnvMap( TBOOL a_bEnable )
{
}

TBOOL remaster::SkinShaderDX11::IsHighEndSkinning()
{
	return TTRUE;
}

void remaster::SkinShaderDX11::EnableHighEndSkinning( TBOOL a_bEnable )
{
}

TBOOL remaster::SkinShaderDX11::IsCapableHighEndSkinning()
{
	return TTRUE;
}

TBOOL remaster::SkinShaderDX11::IsLightScattering()
{
	return TFALSE;
}

void remaster::SkinShaderDX11::SetLightScattering( TBOOL a_bEnable )
{
}

TBOOL remaster::SkinShaderDX11::IsAlphaBlendMaterial()
{
	return TFALSE;
}

void remaster::SkinShaderDX11::SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial )
{
}

ASkinMaterial* remaster::SkinShaderDX11::CreateMaterial( const TCHAR* a_szName )
{
	Validate();

	SkinMaterial* pMaterial = new SkinMaterial();
	pMaterial->SetShader( this );

	if ( TNULL != a_szName )
		pMaterial->SetName( a_szName );

	/*if ( SkinShaderDX11::IsAlphaBlendMaterial() )
	{
		auto pAlphaBlendMaterial = new SkinMaterial();
		pAlphaBlendMaterial->SetShader( this );
		pAlphaBlendMaterial->Create( 1 );

		pMaterial->SetAlphaBlendMaterial( pAlphaBlendMaterial );
	}*/

	return pMaterial;
}

ASkinMesh* remaster::SkinShaderDX11::CreateMesh( const TCHAR* a_szName )
{
	Validate();

	auto pMesh = new SkinMesh();
	pMesh->SetOwnerShader( this );

	return pMesh;
}

TINT remaster::SkinShaderDX11::AddLight( const Toshi::TVector3& a_rPosition, TFLOAT a_fIntensity )
{
	return -1;
}

void remaster::SkinShaderDX11::SetLight( TINT a_iIndex, const Toshi::TVector3& a_rPosition, TFLOAT a_fIntensity )
{
}

void remaster::SkinShaderDX11::RemoveLight( TINT a_iIndex )
{
}

TBOOL remaster::SkinShaderDX11::IsEnableRenderEnvMap()
{
	return TTRUE;
}

void remaster::SkinShaderDX11::SetSomeColour( TUINT a_uiR, TUINT a_uiG, TUINT a_uiB, TUINT a_uiA )
{
}

TINT remaster::SkinShaderDX11::SetUnknown1( TINT a_Unknown, TUINT8 a_fAlpha )
{
	return -1;
}

void remaster::SkinShaderDX11::SetUnknown2( TINT a_Unknown )
{
}
