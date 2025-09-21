#include "pch.h"
#include "SkinMaterial.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

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
