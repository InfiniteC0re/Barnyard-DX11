#pragma once

#include <Math/TMatrix44.h>
#include <Render/TCameraObject.h>
#include <Render/TRenderContext.h>

#include <d3d11.h>

namespace remaster
{

static constexpr TINT   CSM_CASCADE_COUNT                           = 3;
// Default (MEDIUM preset) shadow-map sizes. The active sizes are runtime members on
// CSMManager (see m_iResolution / m_aiCascadeResolution) so they can be changed live.
static constexpr TINT   CSM_RESOLUTION                              = 2048;
static constexpr TINT   CSM_CASCADE_RESOLUTION[ CSM_CASCADE_COUNT ] = { 2048, 1024, 512 };

// CSM shadow-map quality presets. The base resolution drives cascade 0; the finer
// cascades keep the default {2048,1024,512}/2048 ratio (HIGH -> {4096,2048,1024}).
enum CSMPreset : TUINT
{
	CSM_PRESET_LOW,    // 1024 base
	CSM_PRESET_MEDIUM, // 2048 base (default)
	CSM_PRESET_HIGH,   // 4096 base

	CSM_PRESET_COUNT,
};
static constexpr TFLOAT CSM_SPLIT_LAMBDA                            = 0.1f;
static constexpr TFLOAT CSM_DEPTH_BIAS_SLOPE                        = 2.0f;
static constexpr TINT   CSM_DEPTH_BIAS_UNITS                        = 5;
static constexpr TFLOAT CSM_RECEIVER_BIAS                           = 0.0007f;
static constexpr TFLOAT CSM_RECEIVER_PLANE_BIAS                     = 1.0f;
static constexpr TFLOAT CSM_CASCADE_BLEND                           = 0.1f;
static constexpr TFLOAT CSM_MIN_SLOPE_DEPTH_BIAS                    = 0.25f;

struct ShadowCBufferData
{
	Toshi::TMatrix44 matLightVP[ CSM_CASCADE_COUNT ];
	TFLOAT           cascadeSplits[ 4 ];
	TFLOAT           shadowParams[ 4 ];
	TFLOAT           shadowFilterParams[ 4 ];
	TFLOAT           cascadeScales[ 4 ];          // x,y,z = per-cascade UV scale (renderRes / CSM_RESOLUTION); w = normal-offset scale
	TFLOAT           cascadeReceiverBias[ 4 ];    // x,y,z = per-cascade receiver depth bias
	TFLOAT           cascadePCFRadius[ 4 ];       // x,y,z = per-cascade PCF kernel radius
	TFLOAT           cascadeWorldTexelSize[ 4 ];  // x,y,z = per-cascade world units per shadow texel (for normal-offset bias)
	TFLOAT           lightDirection[ 4 ];         // xyz = sun travel direction (sun->scene); w = grazing offset cap
	TFLOAT           cloudParams[ 4 ];            // xy = cloud region min (X,Z), z = 1/region size, w = strength (0 = off)
};

class CSMManager
{
public:
	CSMManager();
	~CSMManager();

	TBOOL Create();
	void  Destroy();

	// Reallocates the shadow atlas at the given quality preset. Safe to call between
	// frames (outside a scene); recomputes the per-cascade sub-resolutions and rebuilds
	// the depth texture, cascade DSVs and SRV. The shader-side scale/texel data is
	// refreshed every frame from these members in UpdateCascades, so no shader changes.
	TBOOL ApplyResolution( CSMPreset a_ePreset );

	TINT      GetResolution() const { return m_iResolution; }
	CSMPreset GetPreset() const { return m_ePreset; }

	void UpdateCascades( Toshi::TRenderContext* a_pRenderContext );
	void RenderShadowMaps();
	void RenderCustomShadowMap(
	    const Toshi::TMatrix44&                        a_rcLightView,
	    const Toshi::TMatrix44&                        a_rcLightProjection,
	    const Toshi::TRenderContext::PROJECTIONPARAMS& a_rcProjectionParams,
	    Toshi::TRenderContext::CameraMode              a_eCameraMode,
	    ID3D11DepthStencilView*                        a_pDepthStencilView,
	    TINT                                           a_iResolution
	);

	TBOOL IsRenderingShadowPass() const { return m_bRenderingShadowPass; }
	const Toshi::TMatrix44& GetCurrentLightProjection() const { return m_LightProj[ m_iCurrentCascade ]; }
	const Toshi::TMatrix44& GetCurrentLightViewProj() const { return m_LightViewProj[ m_iCurrentCascade ]; }
	TINT GetCurrentCascade() const { return m_iCurrentCascade; }

	const ShadowCBufferData& GetShadowCBufferData() const { return m_oShadowCBufferData; }
	const Toshi::TVector3&   GetLightDirection() const { return m_LightView.AsBasisVector3( 2 ); }
	ID3D11ShaderResourceView* GetShadowSRV() const { return m_pShadowSRV; }
	ID3D11SamplerState* GetShadowSampler() const { return m_pShadowSampler; }

private:
	void BuildLightView( Toshi::TRenderContext* a_pRenderContext );
	void BuildCascade( Toshi::TRenderContext* a_pRenderContext, TINT a_iCascade, TFLOAT a_fNearZ, TFLOAT a_fFarZ );
	void BuildOrthoMatrix( Toshi::TMatrix44& a_rOutMatrix, TFLOAT a_fMinX, TFLOAT a_fMaxX, TFLOAT a_fMinY, TFLOAT a_fMaxY, TFLOAT a_fMinZ, TFLOAT a_fMaxZ );
	void RenderSceneCasters( const Toshi::TRenderContext::PROJECTIONPARAMS* a_pProjectionParams, Toshi::TRenderContext::CameraMode a_eCameraMode );

private:
	ID3D11Texture2D*          m_pShadowTexture;
	ID3D11DepthStencilView*   m_pCascadeDSV[ CSM_CASCADE_COUNT ];
	ID3D11ShaderResourceView* m_pShadowSRV;
	ID3D11RasterizerState*    m_pShadowRasterizerState;
	ID3D11SamplerState*       m_pShadowSampler;

	Toshi::TMatrix44     m_LightView;
	Toshi::TVector3      m_oCameraWorldPos; // gameplay camera world position (cloud region centre follows it)
	Toshi::TMatrix44     m_LightProj[ CSM_CASCADE_COUNT ];
	Toshi::TMatrix44     m_LightViewProj[ CSM_CASCADE_COUNT ];
	TFLOAT               m_CascadeSplits[ CSM_CASCADE_COUNT ];
	TFLOAT               m_CascadeWorldTexelSize[ CSM_CASCADE_COUNT ]; // world units per shadow texel; persists across staggered updates
	ShadowCBufferData    m_oShadowCBufferData;
	Toshi::TCameraObject m_oLightCamera;
	TINT                 m_iCurrentCascade;
	TBOOL                m_bRenderingShadowPass;

	// Staggered cascade updates: cascade 0 every frame, cascade 1 every 4 frames,
	// cascade 2 every 8 frames. Skipped cascades keep their previous depth and
	// light matrices (the world is mostly static, so re-rendering them every frame
	// is wasted fill). UpdateCascades computes the per-frame render mask; both it
	// and RenderShadowMaps honour it so the stored depth always matches matLightVP.
	TUINT m_uiFrameCounter;
	TUINT m_uiCascadeRenderMask; // bit i set => cascade i is (re)built/rendered this frame
	TBOOL m_bForceAllCascades;   // forces a full update (first frame, leaving debug view)
	TINT  m_iLastDebugCascade;   // detects debug-cascade changes to trigger a full update

	// Runtime shadow-map sizing (driven by ApplyResolution); seeded to the MEDIUM preset.
	CSMPreset m_ePreset;
	TINT      m_iResolution;                            // atlas texture size (square)
	TINT      m_aiCascadeResolution[ CSM_CASCADE_COUNT ]; // per-cascade sub-rect size
};

extern CSMManager* g_pCSMManager;
extern TBOOL       g_bCSMEnabled;
extern TBOOL       g_bInMainScenePass;
extern TBOOL       g_bReflectionCaptureActive;
extern TINT        g_iCSMDebugCascade;
extern TBOOL       g_bCSMDebugFullRange;
extern TBOOL       g_bCSMDebugMaskBySplit;
extern TBOOL       g_bOverrideSunDirection;
extern TBOOL       g_bCSMDelayedCascadeUpdate;
extern TFLOAT      g_flSunAzimuth;
extern TFLOAT      g_flSunElevation;

// Global shadow tunables (apply to all cascades).
extern TFLOAT      g_flShadowIntensity;
extern TFLOAT      g_flShadowDistance;
extern TFLOAT      g_flShadowSplitLambda;             // 0 = uniform splits, 1 = logarithmic splits
extern TFLOAT      g_flShadowCascadeBlend;
extern TFLOAT      g_flShadowMinSlopeScaledDepthBias; // floor applied to per-cascade slope bias
extern TFLOAT      g_flShadowReceiverPlaneBias;
extern TFLOAT      g_flShadowNormalOffsetScale;       // normal-offset bias in shadow texels (scales per-cascade world texel size)
extern TFLOAT      g_flShadowGrazingScale;            // cap on the 1/NdotL multiplier applied to the normal offset at grazing angles

// Animated cloud shadows. The bake target + sampler live in ERRenderWrapper (post
// passes own the render pipeline); the receivers bind them in StartFlush.
extern TBOOL       g_bCloudShadowsEnabled;
extern TBOOL       g_bCloudShadowsVolumetrics;  // also apply cloud shadows in the volumetric fog
extern TFLOAT      g_flCloudShadowStrength;     // 0..1 darkening applied to the sun term
extern TFLOAT      g_flCloudShadowRegionSize;   // world size (m) of the camera-centred bake region
extern TFLOAT      g_flCloudShadowFeatureScale; // noise frequency (smaller = bigger clouds)
extern TFLOAT      g_flCloudShadowCoverage;     // base cloud coverage threshold
extern TFLOAT      g_flCloudShadowDensity;      // cloud opacity multiplier
extern TFLOAT      g_flCloudShadowContrast;     // hardness of the cloud edges
extern TFLOAT      g_flCloudShadowSpeed;        // wind scroll speed
extern TFLOAT      g_flCloudShadowWindDir[ 2 ]; // wind direction (XZ)
extern ID3D11ShaderResourceView* g_pCloudShadowSRV;
extern ID3D11SamplerState*       g_pCloudShadowSampler;

// Reflection cubemap captured each frame; world shader samples it for environment specular
extern ID3D11ShaderResourceView* g_pSkyCubeSRV;
extern TINT                      g_iSkyCubeMaxMip;

// Per-cascade shadow tunables.
extern TFLOAT      g_aflShadowCasterPadding[ CSM_CASCADE_COUNT ];
extern TFLOAT      g_aflShadowCascadePadding[ CSM_CASCADE_COUNT ];
extern TFLOAT      g_aflShadowSlopeScaledDepthBias[ CSM_CASCADE_COUNT ];
extern TFLOAT      g_aflShadowReceiverBias[ CSM_CASCADE_COUNT ];
extern TFLOAT      g_aflShadowPCFRadius[ CSM_CASCADE_COUNT ];

} // namespace remaster
