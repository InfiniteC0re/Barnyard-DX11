#include "pch.h"
#include "LightData.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

remaster::FrameAllocator<LightDataPacket, 4096>  s_LightDataAllocator;
remaster::FrameAllocator<LightDataPacket, 4096>* g_pLightDataPacketAllocator = &s_LightDataAllocator;

