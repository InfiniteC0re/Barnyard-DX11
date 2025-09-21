#include "pch.h"
#include "WorldMaterial.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

remaster::WorldMaterial::WorldMaterial()
{
}

remaster::WorldMaterial::~WorldMaterial()
{
}

void remaster::WorldMaterial::OnDestroy()
{
}

void remaster::WorldMaterial::PreRender()
{
}

void remaster::WorldMaterial::PostRender()
{
}

TBOOL remaster::WorldMaterial::Create( BLENDMODE a_eBlendMode )
{
	SetBlendMode( a_eBlendMode );
	return AWorldMaterial::Create( a_eBlendMode );
}

void remaster::WorldMaterial::SetBlendMode( BLENDMODE a_eBlendMode )
{
}

void remaster::WorldMaterial::CopyToAlphaBlendMaterial()
{
}
