#pragma once
#include <Toshi/Toshi.h>

// Disk layer for the LightManager's static point lights, keyed by level name in
// Data\StaticLights.xml. Load runs automatically on level change (see ERRenderWrapper)

namespace remaster
{

// Clears existing static lights, then loads the current level's set from the file
void StaticLights_LoadForCurrentLevel( const TCHAR* a_szPath );

// Writes the current static lights under the current level, keeping the other levels
void StaticLights_SaveCurrentLevel( const TCHAR* a_szPath );

} // namespace remaster
