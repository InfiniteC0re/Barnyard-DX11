#pragma once
#include "RenderDX11.h"

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

void  OverrideDisplayResolution( TUINT a_uiWidth, TUINT a_uiHeight );
void  OverrideDisplayMode( DisplayMode a_eMode );
TBOOL IsDisplayResolutionForced();

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

// User effect toggles, indexed for building settings UIs
TINT  GetEffectCount();
TBOOL GetEffectEnabled( TINT a_iIndex );
void  SetEffectEnabled( TINT a_iIndex, TBOOL a_bEnabled );

} // namespace GameSettings

} // namespace remaster
