#include "pch.h"
#include "Viewport.h"

#include <AHooks.h>
#include <HookHelpers.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x006d7fe0, Toshi::TViewport, TViewport_BeginSKU, void )
{
	return;
}

void remaster::SetupRenderHooks_Viewport()
{
	InstallHook<TViewport_BeginSKU>();
}
