#pragma once
#include "FrameAllocator.h"

#include <Render/TRenderContext.h>

struct LightDataPacket
{
	Toshi::TLightIDList oDynamicLights;
	Toshi::TLightIDList oStaticLights;
};

extern remaster::FrameAllocator<LightDataPacket, 4096>* g_pLightDataPacketAllocator;
