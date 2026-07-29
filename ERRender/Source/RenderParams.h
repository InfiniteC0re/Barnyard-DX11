#pragma once
#include <Toshi/Typedefs.h>
#include <ToshiTools/T2CommandLine.h>

// Shared render/graphics tunables, edited live from the editor UI and read by the render
// passes and shader binders. Defined in ERRenderWrapper.cpp, except g_bEnvSpecular (CSMManager.cpp)

namespace remaster
{

// Sun shafts
extern TBOOL  g_bSunShaftsEnabled;
extern TFLOAT g_flSunShaftsAlpha;
extern TFLOAT g_flSunShaftsRaysLength;
extern TFLOAT g_flSunShaftsTint[ 3 ];
extern TINT   g_iSunShaftsKawaseLevels;
extern TFLOAT g_flSunShaftsKawaseOffset;

// Glow bloom
extern TBOOL  g_bGlowBloomEnabled;
extern TINT   g_iGlowBloomKawaseLevels;
extern TFLOAT g_flGlowBloomKawaseOffset;
extern TFLOAT g_flGlowBloomIntensity;

// HDR bloom
extern TBOOL  g_bHDRBloomEnabled;
extern TINT   g_iHDRBloomKawaseLevels;
extern TFLOAT g_flHDRBloomKawaseOffset;
extern TFLOAT g_flHDRBloomThreshold;
extern TFLOAT g_flHDRBloomIntensity;

// Ambient occlusion (g_iAOAlgorithm selects HBAO vs XeGTAO)
extern TBOOL  g_bHBAOEnabled;
extern TBOOL  g_bHBAODebug;
extern TINT   g_iAOAlgorithm;
extern TFLOAT g_flHBAORadius;
extern TFLOAT g_flHBAOSceneScale;
extern TFLOAT g_flHBAOBias;
extern TFLOAT g_flHBAOIntensity;
extern TFLOAT g_flHBAOPower;
extern TFLOAT g_flHBAOBlurSharpness;
extern TFLOAT g_flXeGTAORadiusMultiplier;
extern TFLOAT g_flXeGTAOFalloffRange;
extern TFLOAT g_flXeGTAOSampleDistributionPower;

// Screen-space reflections
extern TBOOL  g_bSSREnabled;
extern TBOOL  g_bSSRDebug;
extern TBOOL  g_bSSRDebugNormals;
extern TFLOAT g_flSSRIntensity;
extern TFLOAT g_flSSRMaxDistance;
extern TFLOAT g_flSSRThickness;
extern TFLOAT g_flSSRStepSize;
extern TINT   g_iSSRMaxSteps;
extern TFLOAT g_flSSRFresnelPower;
extern TFLOAT g_flSSREdgeFade;
extern TFLOAT g_flSSRSurfaceFadeDistance;

// Sky cube reflections
extern TBOOL  g_bSkyCubeEnabled;
extern TBOOL  g_bSkyCubeDebugView;
extern TFLOAT g_flSkyCubeIntensity;
extern TBOOL  g_bReflectTerrain;
extern TFLOAT g_flSkyCubeParallaxHorizontal; // parallax box half-extent X/Z, shared by SSR + world/skin IBL
extern TFLOAT g_flSkyCubeParallaxVertical;   // parallax box half-extent Y
extern TFLOAT g_flSkyCubeBlendTime;          // cross-fade duration in seconds (0 = instant)
extern TFLOAT g_flSkyCubeRefreshTime;        // full-cube refresh cadence in seconds (faces round-robin)
extern TBOOL  g_bEnvSpecular;

// Wind
extern TBOOL  g_bWindEnabled;

extern const Toshi::T2CommandLine* g_pCommandLine;

extern TFLOAT g_flWindStrength;              // max world-unit sway at blue = 1
extern TFLOAT g_flWindSpeed;                 // phase advance per second
extern TFLOAT g_flWindDir[ 2 ];              // world-space XZ sway direction
extern TFLOAT g_flWindTime;                  // accumulated phase, updated per frame

// Volumetric fog
extern TBOOL  g_bVolumetricFogEnabled;
extern TBOOL  g_bVolumetricFogUseSceneColor;
extern TFLOAT g_flVolumetricFogNoiseScale;
extern TFLOAT g_flVolumetricFogNoiseStrength;
extern TFLOAT g_flVolumetricFogWindDir[ 2 ];
extern TFLOAT g_flVolumetricFogWindSpeed;
extern TFLOAT g_flVolumetricFogHeight;       // bottom of the fog (full density at/below)
extern TFLOAT g_flVolumetricFogTopHeight;    // top of the fog (0 at/above; <= bottom disables the height band)
extern TFLOAT g_flVolumetricFogDensity;
extern TFLOAT g_flVolumetricFogG;
extern TFLOAT g_flVolumetricFogMaxDist;
extern TFLOAT g_flVolumetricFogStepGrowth;
extern TFLOAT g_flVolumetricFogIntensity;
extern TFLOAT g_flVolumetricFogColor[ 3 ];

// Boot warm-up screen (set by BootState after a grace period so cached boots don't flash it)
extern TBOOL g_bBootScreenVisible;

// Front-to-back depth sort
extern TBOOL g_bDepthSortOrderTables;

// Debug
extern TBOOL  g_bDebugTangents;

} // namespace remaster
