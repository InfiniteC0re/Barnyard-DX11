#pragma once
#include <Toshi/Typedefs.h>

namespace remaster
{

struct GraphicsSettings;

namespace GameSettings
{

void Load( const TCHAR* a_szPath );
void Save( const TCHAR* a_szPath, const GraphicsSettings& a_rcDisplay );

TBOOL                   HasDisplaySettings();
const GraphicsSettings& GetDisplaySettings();

// UI window
extern TBOOL g_bRenderUI;
void         Render();

TBOOL IsCSMEnabled();
TBOOL AreCloudShadowsEnabled();
TBOOL IsAOEnabled();
TBOOL IsSSREnabled();
TBOOL IsSkyCubeEnabled();
TBOOL IsEnvSpecularEnabled();
TBOOL IsGlowBloomEnabled();
TBOOL IsHDRBloomEnabled();
TBOOL AreSunShaftsEnabled();
TBOOL IsVolumetricFogEnabled();
TBOOL AreDynamicLightsEnabled();
TBOOL AreDynamicLightShadowsEnabled();
TBOOL IsWindEnabled();

} // namespace GameSettings

} // namespace remaster
