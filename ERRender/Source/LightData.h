#pragma once
#include "FrameAllocator.h"

#include <Render/TRenderContext.h>
#include <StaticLights.h>

struct LightDataPacket
{
	Toshi::TLightIDList oDynamicLights;                    // dynamic (glow) lights, cap 4
	TINT8               oStaticLights[ MAX_CELL_STATIC_LIGHTS ]; // static lights, -1 = empty
};

extern remaster::FrameAllocator<LightDataPacket, 4096>* g_pLightDataPacketAllocator;
