#include "pch.h"
#include "SkinMaterial.h"
#include "Resource/ClassPatcher.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::SkinMaterial, 0x0079a618 );

remaster::SkinMaterial::SkinMaterial()
{
}

remaster::SkinMaterial::~SkinMaterial()
{
}

void remaster::SkinMaterial::OnDestroy()
{
}

void remaster::SkinMaterial::PreRender()
{
}

void remaster::SkinMaterial::PostRender()
{
}

TBOOL remaster::SkinMaterial::Create( BLENDMODE a_eBlendMode )
{
	return TTRUE;
}

void remaster::SkinMaterial::SetBlendMode( BLENDMODE a_eBlendMode )
{
}

void remaster::SkinMaterial::CopyToAlphaBlendMaterial()
{
}
