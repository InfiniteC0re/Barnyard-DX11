#include "pch.h"
#include "CSM/CSMManager.h"

#include "RenderDX11.h"
#include "Shader/WorldShader.h"

#include <HookHelpers.h>
#include <BYardSDK/ACamera.h>
#include <BYardSDK/SDKHooks.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster
{

CSMManager* g_pCSMManager                     = TNULL;
TBOOL       g_bCSMEnabled                     = TTRUE;
TINT        g_iCSMDebugCascade                = -1;
TBOOL       g_bCSMDebugFullRange              = TTRUE;
TBOOL       g_bCSMDebugMaskBySplit            = TTRUE;
TBOOL       g_bOverrideSunDirection           = TTRUE;
TFLOAT      g_flSunAzimuth                    = 9.0f;
TFLOAT      g_flSunElevation                  = 139.0f;
TFLOAT      g_flShadowIntensity               = 0.185f;
TFLOAT      g_flShadowDistance                = 56.0f;
TFLOAT      g_flShadowSplitLambda             = CSM_SPLIT_LAMBDA;
TFLOAT      g_flShadowCascadeBlend            = CSM_CASCADE_BLEND;
TFLOAT      g_flShadowMinSlopeScaledDepthBias = CSM_MIN_SLOPE_DEPTH_BIAS;
TFLOAT      g_flShadowReceiverPlaneBias       = CSM_RECEIVER_PLANE_BIAS;
TFLOAT      g_flShadowNormalOffsetScale       = 2.0f;
TFLOAT      g_flShadowGrazingScale            = 1.0f;

TBOOL       g_bCloudShadowsEnabled            = TTRUE;
TBOOL       g_bCloudShadowsVolumetrics        = TFALSE;
TFLOAT      g_flCloudShadowStrength           = 0.8f;
TFLOAT      g_flCloudShadowRegionSize         = 150.0f;
TFLOAT      g_flCloudShadowFeatureScale       = 0.032f;
TFLOAT      g_flCloudShadowCoverage           = -0.22f;
TFLOAT      g_flCloudShadowDensity            = 4.0f;
TFLOAT      g_flCloudShadowContrast           = 0.78f;
TFLOAT      g_flCloudShadowSpeed              = 0.025f;
TFLOAT      g_flCloudShadowWindDir[ 2 ]       = { -1.0f, 1.3f };

// Per-cascade defaults.
TFLOAT      g_aflShadowCasterPadding[ CSM_CASCADE_COUNT ]        = { 3.0f, 3.0f, 3.0f };
TFLOAT      g_aflShadowCascadePadding[ CSM_CASCADE_COUNT ]       = { 8.0f, 8.0f, 8.0f };
TFLOAT      g_aflShadowSlopeScaledDepthBias[ CSM_CASCADE_COUNT ] = { CSM_DEPTH_BIAS_SLOPE, CSM_DEPTH_BIAS_SLOPE, CSM_DEPTH_BIAS_SLOPE };
TFLOAT      g_aflShadowReceiverBias[ CSM_CASCADE_COUNT ]         = { CSM_RECEIVER_BIAS, CSM_RECEIVER_BIAS, CSM_RECEIVER_BIAS };
TFLOAT      g_aflShadowPCFRadius[ CSM_CASCADE_COUNT ]            = { 1.0f, 1.0f, 1.0f };

CSMManager::CSMManager()
    : m_pShadowTexture( TNULL )
    , m_pShadowSRV( TNULL )
    , m_pShadowRasterizerState( TNULL )
    , m_pShadowSampler( TNULL )
    , m_iCurrentCascade( 0 )
    , m_bRenderingShadowPass( TFALSE )
    , m_uiFrameCounter( 0 )
    , m_uiCascadeRenderMask( 0 )
    , m_bForceAllCascades( TTRUE )
    , m_iLastDebugCascade( -1 )
    , m_ePreset( CSM_PRESET_MEDIUM )
    , m_iResolution( CSM_RESOLUTION )
{
	TUtil::MemClear( m_pCascadeDSV, sizeof( m_pCascadeDSV ) );
	TUtil::MemClear( &m_oShadowCBufferData, sizeof( m_oShadowCBufferData ) );
	m_LightView.Identity();

	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
		m_aiCascadeResolution[ i ] = CSM_CASCADE_RESOLUTION[ i ];

	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
	{
		m_LightProj[ i ].Identity();
		m_LightViewProj[ i ].Identity();
		m_CascadeSplits[ i ]         = 0.0f;
		m_CascadeWorldTexelSize[ i ] = 0.0f;
	}
}

CSMManager::~CSMManager()
{
	Destroy();
}

TBOOL CSMManager::Create()
{
	ID3D11Device* pDevice = g_pRender->GetD3D11Device();

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width                = m_iResolution;
	textureDesc.Height               = m_iResolution;
	textureDesc.MipLevels            = 1;
	textureDesc.ArraySize            = CSM_CASCADE_COUNT;
	textureDesc.Format               = DXGI_FORMAT_R16_TYPELESS;
	textureDesc.SampleDesc.Count     = 1;
	textureDesc.SampleDesc.Quality   = 0;
	textureDesc.Usage                = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	DX11_API_VALIDATE_EXIT( pDevice->CreateTexture2D( &textureDesc, TNULL, &m_pShadowTexture ) );

	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format                         = DXGI_FORMAT_D16_UNORM;
		dsvDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsvDesc.Texture2DArray.MipSlice        = 0;
		dsvDesc.Texture2DArray.FirstArraySlice = i;
		dsvDesc.Texture2DArray.ArraySize       = 1;

		DX11_API_VALIDATE_EXIT( pDevice->CreateDepthStencilView( m_pShadowTexture, &dsvDesc, &m_pCascadeDSV[ i ] ) );
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format                          = DXGI_FORMAT_R16_UNORM;
	srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip  = 0;
	srvDesc.Texture2DArray.MipLevels        = 1;
	srvDesc.Texture2DArray.FirstArraySlice  = 0;
	srvDesc.Texture2DArray.ArraySize        = CSM_CASCADE_COUNT;
	DX11_API_VALIDATE_EXIT( pDevice->CreateShaderResourceView( m_pShadowTexture, &srvDesc, &m_pShadowSRV ) );

	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode              = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode              = D3D11_CULL_FRONT;
	rasterizerDesc.DepthClipEnable       = TRUE;
	rasterizerDesc.DepthBias             = CSM_DEPTH_BIAS_UNITS;
	rasterizerDesc.SlopeScaledDepthBias  = g_aflShadowSlopeScaledDepthBias[ 0 ];
	DX11_API_VALIDATE_EXIT( pDevice->CreateRasterizerState( &rasterizerDesc, &m_pShadowRasterizerState ) );

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter             = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc     = D3D11_COMPARISON_LESS_EQUAL;
	samplerDesc.MinLOD             = 0.0f;
	samplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;
	DX11_API_VALIDATE_EXIT( pDevice->CreateSamplerState( &samplerDesc, &m_pShadowSampler ) );

	// Force a full (all-cascade) update on the first frame so every slice has valid
	// depth before it is sampled.
	m_uiFrameCounter      = 0;
	m_uiCascadeRenderMask = 0;
	m_bForceAllCascades   = TTRUE;
	m_iLastDebugCascade   = -1;

	g_pCSMManager = this;
	return TTRUE;
}

void CSMManager::Destroy()
{
	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
	{
		if ( m_pCascadeDSV[ i ] )
		{
			m_pCascadeDSV[ i ]->Release();
			m_pCascadeDSV[ i ] = TNULL;
		}
	}

	if ( m_pShadowSampler )
	{
		m_pShadowSampler->Release();
		m_pShadowSampler = TNULL;
	}

	if ( m_pShadowRasterizerState )
	{
		m_pShadowRasterizerState->Release();
		m_pShadowRasterizerState = TNULL;
	}

	if ( m_pShadowSRV )
	{
		m_pShadowSRV->Release();
		m_pShadowSRV = TNULL;
	}

	if ( m_pShadowTexture )
	{
		m_pShadowTexture->Release();
		m_pShadowTexture = TNULL;
	}

	if ( g_pCSMManager == this )
		g_pCSMManager = TNULL;
}

TBOOL CSMManager::ApplyResolution( CSMPreset a_ePreset )
{
	if ( a_ePreset >= CSM_PRESET_COUNT )
		a_ePreset = CSM_PRESET_MEDIUM;

	// Base atlas size per preset; cascade 0 matches it and the finer cascades keep the
	// default {2048,1024,512}/2048 ratio.
	static const TINT s_aiPresetBase[ CSM_PRESET_COUNT ] = { 1024, 2048, 4096 };

	m_ePreset     = a_ePreset;
	m_iResolution = s_aiPresetBase[ a_ePreset ];

	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
		m_aiCascadeResolution[ i ] = ( m_iResolution * CSM_CASCADE_RESOLUTION[ i ] ) / CSM_RESOLUTION;

	// Rebuild the depth atlas and its views at the new size. Create() re-renders every
	// cascade on the next frame (m_bForceAllCascades), so no stale depth is sampled.
	Destroy();
	return Create();
}

void CSMManager::UpdateCascades( TRenderContext* a_pRenderContext )
{
	TPROFILER_SCOPE();

	if ( !a_pRenderContext ) return;

	m_uiFrameCounter++;

	BuildLightView( a_pRenderContext );

	const auto& rProjectionParams = a_pRenderContext->GetProjectionParams();
	const TFLOAT fNearZ           = TMath::Max( rProjectionParams.m_fNearClip, 0.1f );
	const TFLOAT fFarZ            = TMath::Min( rProjectionParams.m_fFarClip, g_flShadowDistance );
	const TFLOAT fRange           = fFarZ - fNearZ;
	const TFLOAT fRatio           = fFarZ / fNearZ;

	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
	{
		const TFLOAT fProgress     = TFLOAT( i + 1 ) / TFLOAT( CSM_CASCADE_COUNT );
		const TFLOAT fLogSplit     = fNearZ * std::pow( fRatio, fProgress );
		const TFLOAT fUniformSplit = fNearZ + fRange * fProgress;
		m_CascadeSplits[ i ]       = g_flShadowSplitLambda * ( fLogSplit - fUniformSplit ) + fUniformSplit;
	}

	// Decide which cascades to (re)build this frame. A change to the debug cascade
	// (including entering/leaving the debug view) forces a full update so stale
	// slices are refreshed for the new configuration.
	if ( m_iLastDebugCascade != g_iCSMDebugCascade )
		m_bForceAllCascades = TTRUE;
	m_iLastDebugCascade = g_iCSMDebugCascade;

	if ( g_iCSMDebugCascade >= 0 )
	{
		// Debug view: only the inspected cascade is rendered, every frame.
		m_uiCascadeRenderMask = 1u << TMath::Max( 0, TMath::Min( g_iCSMDebugCascade, CSM_CASCADE_COUNT - 1 ) );
	}
	else
	{
		m_uiCascadeRenderMask = 1u << 0; // cascade 0: every frame
		if ( m_bForceAllCascades || ( m_uiFrameCounter % 4 ) == 0 ) m_uiCascadeRenderMask |= 1u << 1; // cascade 1: every 4 frames
		if ( m_bForceAllCascades || ( m_uiFrameCounter % 8 ) == 2 ) m_uiCascadeRenderMask |= 1u << 2; // cascade 2: every 8 frames (offset to avoid colliding with cascade 1)
	}
	m_bForceAllCascades = TFALSE;

	if ( g_iCSMDebugCascade >= 0 )
	{
		const TINT   iDebugCascade = TMath::Max( 0, TMath::Min( g_iCSMDebugCascade, CSM_CASCADE_COUNT - 1 ) );
		const TFLOAT fCascadeNear  = ( g_bCSMDebugFullRange || iDebugCascade == 0 ) ? fNearZ : m_CascadeSplits[ iDebugCascade - 1 ];
		const TFLOAT fCascadeFar   = g_bCSMDebugFullRange ? fFarZ : m_CascadeSplits[ iDebugCascade ];

		if ( g_bCSMDebugFullRange )
			m_CascadeSplits[ iDebugCascade ] = fFarZ;

		BuildCascade( a_pRenderContext, iDebugCascade, fCascadeNear, fCascadeFar );

		for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
		{
			if ( i == iDebugCascade )
				continue;

			m_CascadeSplits[ i ]  = fFarZ;
			m_LightProj[ i ]      = m_LightProj[ iDebugCascade ];
			m_LightViewProj[ i ]  = m_LightViewProj[ iDebugCascade ];
		}
	}
	else
	{
		// Only rebuild the cascades due this frame; the rest keep their previous
		// light matrices so they stay consistent with their retained depth slice.
		for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
		{
			if ( !( m_uiCascadeRenderMask & ( 1u << i ) ) )
				continue;

			const TFLOAT fCascadeNear = ( i == 0 ) ? fNearZ : m_CascadeSplits[ i - 1 ];
			BuildCascade( a_pRenderContext, i, fCascadeNear, m_CascadeSplits[ i ] );
		}
	}

	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
	{
		m_oShadowCBufferData.matLightVP[ i ] = m_LightViewProj[ i ];
		m_oShadowCBufferData.cascadeSplits[ i ] = m_CascadeSplits[ i ];
	}

	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
	{
		m_oShadowCBufferData.cascadeScales[ i ]         = TFLOAT( m_aiCascadeResolution[ i ] ) / TFLOAT( m_iResolution );
		m_oShadowCBufferData.cascadeReceiverBias[ i ]   = g_aflShadowReceiverBias[ i ];
		m_oShadowCBufferData.cascadePCFRadius[ i ]      = g_aflShadowPCFRadius[ i ];
		m_oShadowCBufferData.cascadeWorldTexelSize[ i ] = m_CascadeWorldTexelSize[ i ];
	}

	m_oShadowCBufferData.cascadeSplits[ 3 ]         = 0.0f;
	m_oShadowCBufferData.cascadeScales[ 3 ]         = g_flShadowNormalOffsetScale; // shared normal-offset scale (texels)
	m_oShadowCBufferData.cascadeReceiverBias[ 3 ]   = 0.0f;
	m_oShadowCBufferData.cascadePCFRadius[ 3 ]      = 0.0f;
	m_oShadowCBufferData.cascadeWorldTexelSize[ 3 ] = 0.0f;
	m_oShadowCBufferData.shadowParams[ 0 ]       = g_aflShadowReceiverBias[ 0 ]; // legacy fallback; per-cascade values live in cascadeReceiverBias
	m_oShadowCBufferData.shadowParams[ 1 ]       = 1.0f / TFLOAT( m_iResolution );
	m_oShadowCBufferData.shadowParams[ 2 ]       = g_iCSMDebugCascade >= 0 ? TFLOAT( TMath::Max( 0, TMath::Min( g_iCSMDebugCascade, CSM_CASCADE_COUNT - 1 ) ) + 1 ) : 0.0f;
	m_oShadowCBufferData.shadowParams[ 3 ]       = g_flShadowIntensity;
	m_oShadowCBufferData.shadowFilterParams[ 0 ] = g_aflShadowPCFRadius[ 0 ]; // legacy fallback; per-cascade values live in cascadePCFRadius
	m_oShadowCBufferData.shadowFilterParams[ 1 ] = g_flShadowReceiverPlaneBias;
	m_oShadowCBufferData.shadowFilterParams[ 2 ] = ( g_iCSMDebugCascade >= 0 && !g_bCSMDebugFullRange && g_bCSMDebugMaskBySplit ) ? 1.0f : 0.0f;
	m_oShadowCBufferData.shadowFilterParams[ 3 ] = g_flShadowCascadeBlend;

	// Sun travel direction (sun->scene) + grazing cap, used by the shader to scale the
	// normal-offset bias up on surfaces that are near edge-on to the light.
	const TVector3& rLightDir         = GetLightDirection();
	m_oShadowCBufferData.lightDirection[ 0 ] = rLightDir.x;
	m_oShadowCBufferData.lightDirection[ 1 ] = rLightDir.y;
	m_oShadowCBufferData.lightDirection[ 2 ] = rLightDir.z;
	m_oShadowCBufferData.lightDirection[ 3 ] = TMath::Max( g_flShadowGrazingScale, 1.0f );

	// Cloud shadow region: a world square centred on the camera. The bake pass covers
	// it and receivers map worldPos.xz into [0,1]. Strength 0 disables sampling in-shader.
	const TFLOAT fCloudSize = TMath::Max( g_flCloudShadowRegionSize, 1.0f );
	m_oShadowCBufferData.cloudParams[ 0 ] = m_oCameraWorldPos.x - fCloudSize * 0.5f;
	m_oShadowCBufferData.cloudParams[ 1 ] = m_oCameraWorldPos.z - fCloudSize * 0.5f;
	m_oShadowCBufferData.cloudParams[ 2 ] = 1.0f / fCloudSize;
	m_oShadowCBufferData.cloudParams[ 3 ] = g_bCloudShadowsEnabled ? g_flCloudShadowStrength : 0.0f;
}

void CSMManager::RenderShadowMaps()
{
	TPROFILER_SCOPE();

	if ( !m_pShadowTexture ) return;

	ID3D11DeviceContext* pDeviceContext = g_pRender->GetD3D11DeviceContext();

	g_pRender->PSSetShaderResource( 2, TNULL );
	g_pRender->PSSetShaderResource( 5, TNULL );
	g_pRender->SetColorUpdate( TFALSE );
	g_pRender->SetAlphaUpdate( TFALSE );

	D3D11_VIEWPORT viewport = {};
	viewport.MinDepth       = 0.0f;
	viewport.MaxDepth       = 1.0f;

	const TINT iFirstCascade = g_iCSMDebugCascade >= 0 ? TMath::Max( 0, TMath::Min( g_iCSMDebugCascade, CSM_CASCADE_COUNT - 1 ) ) : 0;
	const TINT iCascadeCount = g_iCSMDebugCascade >= 0 ? iFirstCascade + 1 : CSM_CASCADE_COUNT;
	for ( TINT i = iFirstCascade; i < iCascadeCount; i++ )
	{
		// Skip cascades not scheduled for an update this frame; their depth slice
		// (and matching light matrix) is retained from when it was last rendered.
		if ( !( m_uiCascadeRenderMask & ( 1u << i ) ) )
			continue;

		m_iCurrentCascade = i;

		// Render this cascade into the top-left sub-rect matching its resolution.
		viewport.Width  = TFLOAT( m_aiCascadeResolution[ i ] );
		viewport.Height = TFLOAT( m_aiCascadeResolution[ i ] );
		pDeviceContext->RSSetViewports( 1, &viewport );

		g_pRender->SetRenderTargetView( TNULL, m_pCascadeDSV[ i ] );
		pDeviceContext->ClearDepthStencilView( m_pCascadeDSV[ i ], D3D11_CLEAR_DEPTH, 1.0f, 0 );

		g_pRender->SetDepthEnabled( TTRUE );
		g_pRender->SetDepthWrite( TTRUE );
		g_pRender->SetBlendEnabled( TFALSE );
		g_pRender->SetCullMode( D3D11_CULL_FRONT );
		g_pRender->SetDepthClip( TTRUE );
		g_pRender->SetDepthBias( CSM_DEPTH_BIAS_UNITS );
		g_pRender->SetSlopeScaledDepthBias( TMath::Max( g_aflShadowSlopeScaledDepthBias[ i ], g_flShadowMinSlopeScaledDepthBias ) );

		RenderSceneCasters( TNULL, TRenderContext::CameraMode_Orthographic );
	}

	m_bRenderingShadowPass = TFALSE;
	g_pRender->SetDepthBias( 0 );
	g_pRender->SetSlopeScaledDepthBias( 0.0f );
	g_pRender->SetColorUpdate( TTRUE );
	g_pRender->SetAlphaUpdate( TTRUE );
	g_pRender->ClearStateCache();
}

void CSMManager::RenderCustomShadowMap(
    const Toshi::TMatrix44&                        a_rcLightView,
    const Toshi::TMatrix44&                        a_rcLightProjection,
    const Toshi::TRenderContext::PROJECTIONPARAMS& a_rcProjectionParams,
    Toshi::TRenderContext::CameraMode              a_eCameraMode,
    ID3D11DepthStencilView*                        a_pDepthStencilView,
    TINT                                           a_iResolution
)
{
	TPROFILER_SCOPE();

	if ( !a_pDepthStencilView )
		return;

	ID3D11DeviceContext* pDeviceContext = g_pRender->GetD3D11DeviceContext();

	const TINT      iOldCurrentCascade = m_iCurrentCascade;
	const TMatrix44 oOldLightView      = m_LightView;
	TMatrix44       aOldLightProj[ CSM_CASCADE_COUNT ];
	TMatrix44       aOldLightViewProj[ CSM_CASCADE_COUNT ];
	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
	{
		aOldLightProj[ i ]     = m_LightProj[ i ];
		aOldLightViewProj[ i ] = m_LightViewProj[ i ];
	}

	m_iCurrentCascade    = 0;
	m_LightView          = a_rcLightView;
	m_LightProj[ 0 ]     = a_rcLightProjection;
	m_LightViewProj[ 0 ].Multiply( a_rcLightProjection, a_rcLightView );

	g_pRender->PSSetShaderResource( 2, TNULL );
	g_pRender->PSSetShaderResource( 5, TNULL );
	g_pRender->PSSetShaderResource( 6, TNULL );
	g_pRender->SetColorUpdate( TFALSE );
	g_pRender->SetAlphaUpdate( TFALSE );

	D3D11_VIEWPORT viewport = {};
	viewport.Width          = TFLOAT( a_iResolution );
	viewport.Height         = TFLOAT( a_iResolution );
	viewport.MinDepth       = 0.0f;
	viewport.MaxDepth       = 1.0f;
	pDeviceContext->RSSetViewports( 1, &viewport );

	g_pRender->SetRenderTargetView( TNULL, a_pDepthStencilView );
	pDeviceContext->ClearDepthStencilView( a_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0 );

	g_pRender->SetDepthEnabled( TTRUE );
	g_pRender->SetDepthWrite( TTRUE );
	g_pRender->SetBlendEnabled( TFALSE );
	g_pRender->SetCullMode( D3D11_CULL_FRONT );
	g_pRender->SetDepthClip( TTRUE );
	g_pRender->SetDepthBias( CSM_DEPTH_BIAS_UNITS );
	g_pRender->SetSlopeScaledDepthBias( g_aflShadowSlopeScaledDepthBias[ 0 ] );

	RenderSceneCasters( &a_rcProjectionParams, a_eCameraMode );

	m_bRenderingShadowPass = TFALSE;
	g_pRender->SetDepthBias( 0 );
	g_pRender->SetSlopeScaledDepthBias( 0.0f );
	g_pRender->SetColorUpdate( TTRUE );
	g_pRender->SetAlphaUpdate( TTRUE );
	g_pRender->ClearStateCache();

	m_iCurrentCascade = iOldCurrentCascade;
	m_LightView       = oOldLightView;
	for ( TINT i = 0; i < CSM_CASCADE_COUNT; i++ )
	{
		m_LightProj[ i ]     = aOldLightProj[ i ];
		m_LightViewProj[ i ] = aOldLightViewProj[ i ];
	}
}

void CSMManager::RenderSceneCasters( const Toshi::TRenderContext::PROJECTIONPARAMS* a_pProjectionParams, Toshi::TRenderContext::CameraMode a_eCameraMode )
{
	TPROFILER_SCOPE();

	auto pRenderContext = g_pRender->GetCurrentContext();
	if ( !pRenderContext ) return;

	const TMatrix44 oOldWorldView     = pRenderContext->GetWorldViewMatrix();
	const TMatrix44 oOldModelView     = pRenderContext->GetModelViewMatrix();
	const auto      oOldProjection    = pRenderContext->GetProjectionParams();
	const auto      eOldCameraMode    = pRenderContext->GetCameraMode();
	TCameraObject*  pOldCameraObject  = pRenderContext->GetCameraObject();
	auto&           rTransformStack   = g_pRender->GetTransforms();
	auto            oLightProjection  = oOldProjection;
	const auto&     rLightProjMatrix  = m_LightProj[ m_iCurrentCascade ];

	if ( a_pProjectionParams )
	{
		oLightProjection = *a_pProjectionParams;
	}
	else
	{
		oLightProjection.m_Proj.x    = TMath::Max( rLightProjMatrix.m_f11 * pRenderContext->GetWidth() * 0.5f, 0.0001f );
		oLightProjection.m_Proj.y    = TMath::Max( -rLightProjMatrix.m_f22 * pRenderContext->GetHeight() * 0.5f, 0.0001f );
		oLightProjection.m_Centre.x  = ( rLightProjMatrix.m_f41 + 1.0f ) * pRenderContext->GetWidth() * 0.5f;
		oLightProjection.m_Centre.y  = ( 1.0f - rLightProjMatrix.m_f42 ) * pRenderContext->GetHeight() * 0.5f;
		oLightProjection.m_fNearClip = -rLightProjMatrix.m_f43 / rLightProjMatrix.m_f33;
		oLightProjection.m_fFarClip  = oLightProjection.m_fNearClip + 1.0f / rLightProjMatrix.m_f33;
	}

	m_bRenderingShadowPass = TTRUE;

	pRenderContext->SetProjectionParams( oLightProjection );
	pRenderContext->SetWorldViewMatrix( m_LightView );
	pRenderContext->SetModelViewMatrix( m_LightView );
	pRenderContext->SetCameraMode( a_eCameraMode );
	pRenderContext->Update();
	pRenderContext->SetCameraObject( &m_oLightCamera );

	// Replace matrix in ACamera object, since it is used by some renderer code to cull objects
	ACamera* pCamera = TNULL;
	ACamera  oOldCamera;

	if ( *(void**)0x007822e0 )
	{
		pCamera = CALL_THIS( 0x0045b870, void*, ACamera*, *(void**)0x007822e0 ); // ACameraManager::GetCurrentCamera
		if ( pCamera )
		{
			oOldCamera = *pCamera;
			m_oLightCamera.GetTransformObject().SetMatrix( pCamera->m_Matrix );
		}
	}

	rTransformStack.Reset();
	rTransformStack.PushNull().Identity();
	rTransformStack.Push( m_LightView );

	if ( *(void**)0x007b45fc )
	{
		TPROFILER_NAMED( "AModelRepos::Render" );

		CALL_THIS( 0x006125d0, void*, void, *(void**)0x007b45fc, TINT, 2 ); // AModelRepos::RenderModelsOfType
		CALL_THIS( 0x006125d0, void*, void, *(void**)0x007b45fc, TINT, 1 ); // AModelRepos::RenderModelsOfType
	}

	if ( *(TINT*)0x00796300 )
	{
		{
			TPROFILER_NAMED( "AGateManager::Render" );

			if ( *(TINT*)0x0078de44 )
				CALL_THIS( 0x005dd5c0, void*, void, *(void**)0x0078de44 ); // AGateManager::Render
		}

		{
			TPROFILER_NAMED( "ATerrain::Render" );

			CALL_THIS( 0x005ea8b0, void*, void, *(void**)0x00796300 ); // ATerrain::Render
		}

		{
			TPROFILER_NAMED( "ATreeManager::Render" );

			if ( *(void**)0x00796304 )
				CALL_THIS( 0x005ef3a0, void*, void, *(void**)0x00796304 ); // ATreeManager::Render
		}

		{
			TPROFILER_NAMED( "AInstanceManager::Render" );

			if ( *(void**)0x0078deb0 )
				CALL_THIS( 0x005e17a0, void*, void, *(void**)0x0078deb0 ); // AInstanceManager::Render
		}

		{
			TPROFILER_NAMED( "ARegrowthManager::Render" );

			if ( *(void**)0x007922e0 )
				CALL_THIS( 0x005e3990, void*, void, *(void**)0x007922e0 ); // ARegrowthManager::Render
		}
	}

	{
		TPROFILER_NAMED( "AAnimalPopulationManager::Render" );

		if ( *(void**)0x00783c18 )
			CALL_THIS( 0x0053a320, void*, void, *(void**)0x00783c18, TBOOL, TFALSE ); // AAnimalPopulationManager::Render
	}

	// Flush the per-mesh shadow casters.
	g_pRender->FlushShaders();

	rTransformStack.Reset();
	rTransformStack.PushNull().Identity();
	rTransformStack.Push( oOldWorldView );

	pRenderContext->SetProjectionParams( oOldProjection );
	pRenderContext->SetCameraMode( eOldCameraMode );
	pRenderContext->SetWorldViewMatrix( oOldWorldView );
	pRenderContext->SetModelViewMatrix( oOldModelView );
	pRenderContext->SetCameraObject( pOldCameraObject );
	pRenderContext->Update();

	// Restore camera object
	if ( pCamera ) *pCamera = oOldCamera;

	m_bRenderingShadowPass = TFALSE;
}

void CSMManager::BuildLightView( TRenderContext* a_pRenderContext )
{
	TPROFILER_SCOPE();

	TVector3 lightDir;
	TVector3 right;

	if ( g_bOverrideSunDirection )
	{
		const TFLOAT azRad = g_flSunAzimuth * ( 3.14159265f / 180.0f );
		const TFLOAT elRad = g_flSunElevation * ( 3.14159265f / 180.0f );
		lightDir.x = std::cos( elRad ) * std::sin( azRad );
		lightDir.y = -std::sin( elRad );
		lightDir.z = std::cos( elRad ) * std::cos( azRad );

		// Derive right from azimuth directly. Always horizontal, no degeneracy at high elevation.
		right.x = std::cos( azRad );
		right.y = 0.0f;
		right.z = -std::sin( azRad );
	}
	else
	{
		lightDir = g_pRender->GetLightDirection().AsBasisVector3( 0 );
		lightDir.Normalize();

		TVector3 up = TMath::Abs( lightDir.y ) < 0.99f ? TVector3( 0.0f, 1.0f, 0.0f ) : TVector3( 1.0f, 0.0f, 0.0f );
		right.CrossProduct( up, lightDir );
		right.Normalize();
	}

	TVector3 up;
	up.CrossProduct( lightDir, right );
	up.Normalize();

	TVector3 sceneCenter = a_pRenderContext->GetViewWorldMatrix().GetTranslation3();
	if ( *(void**)0x007822e0 )
	{
		ACamera* pCamera = CALL_THIS( 0x0045b870, void*, ACamera*, *(void**)0x007822e0 ); // ACameraManager::GetCurrentCamera
		if ( pCamera )
			sceneCenter = pCamera->m_Matrix.GetTranslation3();
	}

	// Cache the gameplay camera world position so the cloud shadow region can follow it.
	// The open world means it can't be anchored near the origin.
	m_oCameraWorldPos = sceneCenter;

	m_LightView.Identity();
	m_LightView.AsBasisVector3( 0 ) = right;
	m_LightView.AsBasisVector3( 1 ) = up;
	m_LightView.AsBasisVector3( 2 ) = lightDir;
	m_LightView.m_f41 = -TVector3::DotProduct( right, sceneCenter );
	m_LightView.m_f42 = -TVector3::DotProduct( up, sceneCenter );
	m_LightView.m_f43 = -TVector3::DotProduct( lightDir, sceneCenter );
}

void CSMManager::BuildCascade( TRenderContext* a_pRenderContext, TINT a_iCascade, TFLOAT a_fNearZ, TFLOAT a_fFarZ )
{
	TPROFILER_SCOPE();

	const auto& rProjectionParams = a_pRenderContext->GetProjectionParams();
	const auto& rViewportParams   = a_pRenderContext->GetViewportParameters();
	TMatrix44 oViewWorld = a_pRenderContext->GetViewWorldMatrix();
	if ( *(void**)0x007822e0 )
	{
		ACamera* pCamera = CALL_THIS( 0x0045b870, void*, ACamera*, *(void**)0x007822e0 ); // ACameraManager::GetCurrentCamera
		if ( pCamera )
			oViewWorld = pCamera->m_Matrix;
	}

	const TFLOAT fAspect    = rViewportParams.fWidth / rViewportParams.fHeight;
	const TFLOAT fTanHalfY  = ( rViewportParams.fHeight * 0.5f ) / rProjectionParams.m_Proj.y;
	const TFLOAT fNearHalfY = a_fNearZ * fTanHalfY;
	const TFLOAT fNearHalfX = fNearHalfY * fAspect;
	const TFLOAT fFarHalfY  = a_fFarZ * fTanHalfY;
	const TFLOAT fFarHalfX  = fFarHalfY * fAspect;

	TVector3 aViewCorners[ 8 ] = {
		{ -fNearHalfX, -fNearHalfY, a_fNearZ },
		{ -fNearHalfX,  fNearHalfY, a_fNearZ },
		{  fNearHalfX, -fNearHalfY, a_fNearZ },
		{  fNearHalfX,  fNearHalfY, a_fNearZ },
		{ -fFarHalfX,  -fFarHalfY,  a_fFarZ },
		{ -fFarHalfX,   fFarHalfY,  a_fFarZ },
		{  fFarHalfX,  -fFarHalfY,  a_fFarZ },
		{  fFarHalfX,   fFarHalfY,  a_fFarZ },
	};

	TFLOAT fMinZ = FLT_MAX;
	TFLOAT fMaxZ = -FLT_MAX;
	TVector3 aWorldCorners[ TARRAYSIZE( aViewCorners ) ];
	TVector3 oWorldCenter( 0.0f, 0.0f, 0.0f );

	for ( TINT i = 0; i < TARRAYSIZE( aViewCorners ); i++ )
	{
		TMatrix44::TransformVector( aWorldCorners[ i ], oViewWorld, aViewCorners[ i ] );
		oWorldCenter += aWorldCorners[ i ];
	}

	oWorldCenter.Divide( TFLOAT( TARRAYSIZE( aWorldCorners ) ) );

	TFLOAT fRadius = 0.0f;
	for ( TINT i = 0; i < TARRAYSIZE( aWorldCorners ); i++ )
	{
		TVector3 lightPos;
		TMatrix44::TransformVector( lightPos, m_LightView, aWorldCorners[ i ] );

		fRadius = TMath::Max( fRadius, TVector3::Distance( oWorldCenter, aWorldCorners[ i ] ) );
		fMinZ   = TMath::Min( fMinZ, lightPos.z );
		fMaxZ   = TMath::Max( fMaxZ, lightPos.z );
	}

	fRadius += g_aflShadowCascadePadding[ a_iCascade ];

	TVector3 oLightCenter;
	TMatrix44::TransformVector( oLightCenter, m_LightView, oWorldCenter );

	// fRadius is rotation-invariant (rigid transform preserves corner distances), so the
	// extent and therefore the texel size are constant per cascade.
	const TFLOAT fCascadeRes = TFLOAT( m_aiCascadeResolution[ a_iCascade ] );
	const TFLOAT fTexelSize  = ( 2.0f * fRadius ) / fCascadeRes;

	// Snap the cascade origin to texel increments on a world-anchored grid. m_LightView's
	// translation follows the camera (for depth precision), so snapping oLightCenter
	// directly would anchor the grid to the camera and bring back the classic edge shimmer,
	// worst on the coarse-texel far cascades. Strip the camera-relative translation
	// (m_f41/m_f42) to get the world-space light projection, snap that, then add it back.
	const TFLOAT fWorldCenterLX = oLightCenter.x - m_LightView.m_f41;
	const TFLOAT fWorldCenterLY = oLightCenter.y - m_LightView.m_f42;

	TFLOAT fMinX = std::floor( ( fWorldCenterLX - fRadius ) / fTexelSize ) * fTexelSize + m_LightView.m_f41;
	TFLOAT fMinY = std::floor( ( fWorldCenterLY - fRadius ) / fTexelSize ) * fTexelSize + m_LightView.m_f42;
	TFLOAT fMaxX = fMinX + fCascadeRes * fTexelSize;
	TFLOAT fMaxY = fMinY + fCascadeRes * fTexelSize;

	// World units per shadow texel, used by the shader's normal-offset bias.
	m_CascadeWorldTexelSize[ a_iCascade ] = fTexelSize;

	fMinZ -= fRadius * 2.0f + g_aflShadowCasterPadding[ a_iCascade ];

	BuildOrthoMatrix( m_LightProj[ a_iCascade ], fMinX, fMaxX, fMinY, fMaxY, fMinZ, fMaxZ );
	m_LightViewProj[ a_iCascade ].Multiply( m_LightProj[ a_iCascade ], m_LightView );
}

void CSMManager::BuildOrthoMatrix( TMatrix44& a_rOutMatrix, TFLOAT a_fMinX, TFLOAT a_fMaxX, TFLOAT a_fMinY, TFLOAT a_fMaxY, TFLOAT a_fMinZ, TFLOAT a_fMaxZ )
{
	TPROFILER_SCOPE();

	a_rOutMatrix.Identity();
	a_rOutMatrix.m_f11 = 2.0f / ( a_fMaxX - a_fMinX );
	a_rOutMatrix.m_f22 = 2.0f / ( a_fMinY - a_fMaxY );
	a_rOutMatrix.m_f33 = 1.0f / ( a_fMaxZ - a_fMinZ );
	a_rOutMatrix.m_f41 = -( a_fMinX + a_fMaxX ) / ( a_fMaxX - a_fMinX );
	a_rOutMatrix.m_f42 = ( a_fMinY + a_fMaxY ) / ( a_fMaxY - a_fMinY );
	a_rOutMatrix.m_f43 = -a_fMinZ / ( a_fMaxZ - a_fMinZ );
}

} // namespace remaster
