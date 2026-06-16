#pragma once

#include <Math/TMatrix44.h>
#include <Render/TCameraObject.h>
#include <Render/TRenderContext.h>

#include <d3d11.h>

namespace remaster
{

static constexpr TINT   CSM_CASCADE_COUNT        = 3;
static constexpr TINT   CSM_RESOLUTION           = 4096;
static constexpr TFLOAT CSM_SPLIT_LAMBDA         = 0.5f;
static constexpr TFLOAT CSM_DEPTH_BIAS_SLOPE     = 2.0f;
static constexpr TINT   CSM_DEPTH_BIAS_UNITS     = 100;
static constexpr TFLOAT CSM_RECEIVER_BIAS        = 0.0005f;
static constexpr TFLOAT CSM_RECEIVER_PLANE_BIAS  = 1.0f;
static constexpr TFLOAT CSM_PCF_RADIUS           = 1.0f;
static constexpr TFLOAT CSM_CASCADE_BLEND        = 0.1f;
static constexpr TFLOAT CSM_MIN_SLOPE_DEPTH_BIAS = 0.25f;

struct ShadowCBufferData
{
	Toshi::TMatrix44 matLightVP[ CSM_CASCADE_COUNT ];
	TFLOAT           cascadeSplits[ 4 ];
	TFLOAT           shadowParams[ 4 ];
	TFLOAT           shadowFilterParams[ 4 ];
};

class CSMManager
{
public:
	CSMManager();
	~CSMManager();

	TBOOL Create();
	void  Destroy();

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

	Toshi::TMatrix44 m_LightView;
	Toshi::TMatrix44 m_LightProj[ CSM_CASCADE_COUNT ];
	Toshi::TMatrix44 m_LightViewProj[ CSM_CASCADE_COUNT ];
	TFLOAT           m_CascadeSplits[ CSM_CASCADE_COUNT ];
	ShadowCBufferData m_oShadowCBufferData;
	Toshi::TCameraObject m_oLightCamera;
	TINT                 m_iCurrentCascade;
	TBOOL                m_bRenderingShadowPass;
};

extern CSMManager* g_pCSMManager;
extern TBOOL       g_bCSMEnabled;
extern TINT        g_iCSMDebugCascade;
extern TBOOL       g_bCSMDebugFullRange;
extern TBOOL       g_bCSMDebugMaskBySplit;
extern TBOOL       g_bOverrideSunDirection;
extern TFLOAT      g_flSunAzimuth;
extern TFLOAT      g_flSunElevation;
extern TFLOAT      g_flShadowIntensity;
extern TFLOAT      g_flShadowDistance;
extern TFLOAT      g_flShadowCasterPadding;
extern TFLOAT      g_flShadowCascadePadding;
extern TFLOAT      g_flShadowSlopeScaledDepthBias;
extern TFLOAT      g_flShadowMinSlopeScaledDepthBias;
extern TFLOAT      g_flShadowReceiverBias;
extern TFLOAT      g_flShadowReceiverPlaneBias;
extern TFLOAT      g_flShadowPCFRadius;
extern TFLOAT      g_flShadowCascadeBlend;

} // namespace remaster
