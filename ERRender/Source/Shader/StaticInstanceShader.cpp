#include "pch.h"
#include "StaticInstanceShader.h"

#include <AHooks.h>
#include <HookHelpers.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

struct AStaticInstanceShaderHAL
{

};

MEMBER_HOOK( 0x005f8fd0, AStaticInstanceShaderHAL, AStaticInstanceShaderHAL_Flush, void )
{
}

void remaster::SetupRenderHooks_StaticInstanceShader()
{
	InstallHook<AStaticInstanceShaderHAL_Flush>();
}
