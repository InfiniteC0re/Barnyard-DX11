#include "pch.h"
#include "LightManager.h"
#include "RenderDX11.h"
#include "CSM/CSMManager.h"

#include <Render/TRenderPacket.h>
#include <BYardSDK/AGlowViewport.h>
#include <BYardSDK/ACamera.h>
#include <Toshi/TScheduler.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static AGlowViewport::GlowObject* GetGlowObjectByID( Toshi::TLightID a_iLightID )
{
	AGlowViewport* pGlowViewport = AGlowViewport::GetSingleton();
	if ( !pGlowViewport || a_iLightID < 0 )
		return TNULL;

	for ( AGlowViewport::GlowObject* pObj = pGlowViewport->m_pHeadUsedObject; pObj != TNULL; pObj = pObj->m_pNextObject )
	{
		if ( pObj->m_iID == a_iLightID )
			return pObj;
	}

	return TNULL;
}

static TBOOL BuildGlowObjectWorldTransform( Toshi::TMatrix44& a_rTransform, AGlowViewport::GlowObject* a_pGlowObject )
{
	if ( !a_pGlowObject )
		return TFALSE;

	// Editor-placed lights have no scene object, so m_oTransform already holds the world transform.
	if ( !a_pGlowObject->m_pSceneObject )
	{
		a_rTransform = a_pGlowObject->m_oTransform;
		return TTRUE;
	}

	TModelInstance* pModelInstance = a_pGlowObject->m_pSceneObject->GetInstance();
	if ( !pModelInstance )
		return TFALSE;

	TSkeletonInstance* pSkeletonInstance = pModelInstance->GetSkeletonInstance();
	if ( !pSkeletonInstance )
		return TFALSE;

	TMatrix44 oBoneTransform;
	pSkeletonInstance->GetBoneTransformCurrent( a_pGlowObject->m_iAttachBone, oBoneTransform );

	a_pGlowObject->m_pSceneObject->GetTransform().GetLocalMatrixImp( a_rTransform );
	a_rTransform.Multiply( oBoneTransform );

	TVector4 vNewForwardVec = a_rTransform.AsBasisVector4( BASISVECTOR_UP );
	vNewForwardVec.Negate4();

	a_rTransform.AsBasisVector4( BASISVECTOR_UP )      = a_rTransform.AsBasisVector4( BASISVECTOR_FORWARD );
	a_rTransform.AsBasisVector4( BASISVECTOR_FORWARD ) = vNewForwardVec;

	a_rTransform.Multiply( a_pGlowObject->m_oTransform );

	switch ( a_pGlowObject->m_eTransformType )
	{
		case -3:
			a_rTransform.Scale( -1.0f, -1.0f, -1.0f );
			break;
		case -2:
			a_rTransform.Scale( -1.0f, -1.0f, -1.0f );
		case 1:
			TVector4::Swap( a_rTransform.AsBasisVector4( BASISVECTOR_UP ), a_rTransform.AsBasisVector4( BASISVECTOR_FORWARD ) );
			TVector4::Swap( a_rTransform.AsBasisVector4( BASISVECTOR_RIGHT ), a_rTransform.AsBasisVector4( BASISVECTOR_UP ) );
			break;
		case -1:
			a_rTransform.Scale( -1.0f, -1.0f, -1.0f );
			TVector4::Swap( a_rTransform.AsBasisVector4( BASISVECTOR_RIGHT ), a_rTransform.AsBasisVector4( BASISVECTOR_FORWARD ) );
			TVector4::Swap( a_rTransform.AsBasisVector4( BASISVECTOR_RIGHT ), a_rTransform.AsBasisVector4( BASISVECTOR_UP ) );
			break;
		case 0:
			TVector4::Swap( a_rTransform.AsBasisVector4( BASISVECTOR_RIGHT ), a_rTransform.AsBasisVector4( BASISVECTOR_FORWARD ) );
			TVector4::Swap( a_rTransform.AsBasisVector4( BASISVECTOR_RIGHT ), a_rTransform.AsBasisVector4( BASISVECTOR_UP ) );
			break;
	}

	return TTRUE;
}

namespace remaster
{

LightManager* g_pLightManager = TNULL;

TBOOL  g_bDynamicLightEnabled              = TTRUE;
TFLOAT g_flDynamicLightIntensity           = 1.75f;
TFLOAT g_flDynamicLightVolumetricIntensity = 0.3f;
TFLOAT g_flDynamicLightColor[ 3 ]          = { 255.0f / 255.0f, 181.0f / 255.0f, 110.0f / 255.0f };
TBOOL  g_bDynamicLightShadowsEnabled   = TTRUE;
TFLOAT g_flDynamicLightShadowDistance  = 45.0f;
TFLOAT g_flDynamicLightShadowIntensity = 1.0f;
TFLOAT g_flDynamicLightShadowBias      = 0.0015f;
TFLOAT g_flDynamicLightBumpScale       = 3.5f;
TBOOL  g_bDynamicLightFlickerEnabled   = TTRUE;
TFLOAT g_flDynamicLightFlickerSpeed    = 5.0f;
TFLOAT g_flDynamicLightFlickerStrength = 0.1f;

// A gentle, never-quite-repeating brightness wobble. Three sine waves at unrelated
// frequencies (a slow swell, a mid flutter and a fast crackle) keep it from looking
// mechanical. Each light flickers on its own settings.
static TFLOAT ComputeFlickerMultiplier( const DynamicLightSettings& a_rSettings )
{
	if ( !a_rSettings.bFlickerEnabled )
		return 1.0f;

	TSystemManager* pSystemManager = TREINTERPRETCAST( TSystemManager*, 0x007ce640 );
	const TFLOAT    fTime          = pSystemManager->GetScheduler()->GetTotalTime();
	const TFLOAT    fS             = fTime * a_rSettings.flFlickerSpeed;

	const TFLOAT fFlicker =
	    TMath::Sin( fS * 1.00f ) * 0.50f +
	    TMath::Sin( fS * 2.71f ) * 0.30f +
	    TMath::Sin( fS * 7.33f ) * 0.20f;

	return TMath::Max( 0.0f, 1.0f + fFlicker * a_rSettings.flFlickerStrength );
}

// Writes one light into a cbuffer slot. The caller resolves the light's place in the
// frame's shadow list (a_iShadowIndex, -1 when it casts none) and the matching
// view-projection table, since both come from LightManager's per-frame state.
static TBOOL FillDynamicLightCBufferEntry(
    DynamicLightCBuffer&           a_rCBuffer,
    TINT                           a_iLightIndex,
    AGlowViewport::GlowObject*     a_pGlowObject,
    TBOOL                          a_bRequireShadow,
    const DynamicLightSettings&    a_rSettings,
    TFLOAT                         a_flFlicker,
    TINT                           a_iShadowIndex,
    const Toshi::TMatrix44*        a_pShadowViewProj
)
{
	TMatrix44 oGlowTransform;
	if ( !a_pGlowObject || !a_pGlowObject->IsEnabled() || !BuildGlowObjectWorldTransform( oGlowTransform, a_pGlowObject ) )
		return TFALSE;

	if ( a_bRequireShadow && a_iShadowIndex < 0 )
		return TFALSE;

	const TFLOAT flRadius   = TMath::Max( a_pGlowObject->m_oProjectionParams.m_fFarClip, 1.0f );
	TVector4     vDirection = oGlowTransform.AsBasisVector4( BASISVECTOR_FORWARD );
	vDirection.Normalise();

	// Recover the cone half-angles from the light's projection. The 0.25 cancels the 2x2
	// virtual viewport the editor lights are built with (see Editor's BuildLightProjection).
	const TFLOAT flHalfWidth  = a_pGlowObject->m_oViewportParams.fWidth * 0.25f;
	const TFLOAT flHalfHeight = a_pGlowObject->m_oViewportParams.fHeight * 0.25f;
	const TFLOAT flTanHalfX   = flHalfWidth / TMath::Max( a_pGlowObject->m_oProjectionParams.m_Proj.x, 0.001f );
	const TFLOAT flTanHalfY   = flHalfHeight / TMath::Max( a_pGlowObject->m_oProjectionParams.m_Proj.y, 0.001f );
	const TFLOAT flTanOuter   = TMath::Max( flTanHalfX, flTanHalfY );
	const TFLOAT flTanInner   = flTanOuter * 0.4f;
	const TFLOAT flCosOuter   = 1.0f / TMath::Sqrt( 1.0f + flTanOuter * flTanOuter );
	const TFLOAT flCosInner   = 1.0f / TMath::Sqrt( 1.0f + flTanInner * flTanInner );

	a_rCBuffer.lightPositionRadius[ a_iLightIndex ] = TVector4(
	    oGlowTransform.GetTranslation().x,
	    oGlowTransform.GetTranslation().y,
	    oGlowTransform.GetTranslation().z,
	    flRadius
	);

	a_rCBuffer.lightDirectionCone[ a_iLightIndex ] = TVector4(
	    vDirection.x,
	    vDirection.y,
	    vDirection.z,
	    flCosOuter
	);

	// Colour and intensity, both scaled by the flicker.
	a_rCBuffer.lightColor[ a_iLightIndex ] = TVector4(
	    a_rSettings.flColor[ 0 ],
	    a_rSettings.flColor[ 1 ],
	    a_rSettings.flColor[ 2 ],
	    a_rSettings.flVolumetricIntensity * a_flFlicker
	);

	a_rCBuffer.lightIntensity[ a_iLightIndex ] = TVector4(
	    a_rSettings.flSurfaceIntensity * a_flFlicker,
	    a_rSettings.flBumpScale,
	    flCosInner,
	    0.0f
	);

	if ( a_iShadowIndex >= 0 )
	{
		a_rCBuffer.matLightVP[ a_iLightIndex ] = a_pShadowViewProj[ a_iShadowIndex ];

		if ( g_bDynamicLightShadowsEnabled )
		{
			a_rCBuffer.shadowParams[ a_iLightIndex ] = TVector4(
			    TFLOAT( a_iShadowIndex ),
			    1.0f / TFLOAT( DYNAMIC_LIGHT_SHADOW_RESOLUTION ),
			    a_rSettings.flShadowBias,
			    a_rSettings.flShadowIntensity
			);
		}
		else
		{
			a_rCBuffer.shadowParams[ a_iLightIndex ] = TVector4( -1.0f, 0.0f, 0.0f, 0.0f );
		}
	}
	else
	{
		a_rCBuffer.shadowParams[ a_iLightIndex ] = TVector4( -1.0f, 0.0f, 0.0f, 0.0f );
	}

	return TTRUE;
}

//-----------------------------------------------------------------------------
// LightManager
//-----------------------------------------------------------------------------

LightManager::LightManager()
    : m_pLightBuffer( TNULL )
    , m_pShadowTexture( TNULL )
    , m_pShadowSRV( TNULL )
    , m_pShadowSampler( TNULL )
    , m_iNumShadowLights( 0 )
    , m_bPreviousBufferValid( TFALSE )
    , m_iNumStaticPointLights( 0 )
    , m_pStaticLightBuffer( TNULL )
{
	TUtil::MemClear( m_apShadowDSV, sizeof( m_apShadowDSV ) );
	TUtil::MemClear( m_aiShadowLightIDs, sizeof( m_aiShadowLightIDs ) );
	TUtil::MemClear( &m_oPreviousBuffer, sizeof( m_oPreviousBuffer ) );
	TUtil::MemClear( m_aDynamicLightSettings, sizeof( m_aDynamicLightSettings ) );
	TUtil::MemClear( m_aStaticPointLights, sizeof( m_aStaticPointLights ) );

	for ( TINT i = 0; i < DYNAMIC_LIGHT_COUNT; i++ )
		m_aShadowLightViewProj[ i ].Identity();
}

LightManager::~LightManager()
{
	Destroy();
}

TBOOL LightManager::Create()
{
	if ( !CreateShadowResources() )
		return TFALSE;

	if ( !CreateLightsCBuffer() )
		return TFALSE;

	if ( !CreateStaticLightCBuffer() )
		return TFALSE;

	g_pLightManager = this;
	return TTRUE;
}

void LightManager::Destroy()
{
	for ( TINT i = 0; i < DYNAMIC_LIGHT_COUNT; i++ )
	{
		if ( m_apShadowDSV[ i ] )
		{
			m_apShadowDSV[ i ]->Release();
			m_apShadowDSV[ i ] = TNULL;
		}
	}

	if ( m_pShadowSampler )
	{
		m_pShadowSampler->Release();
		m_pShadowSampler = TNULL;
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

	if ( m_pLightBuffer )
	{
		m_pLightBuffer->Release();
		m_pLightBuffer = TNULL;
	}

	if ( m_pStaticLightBuffer )
	{
		m_pStaticLightBuffer->Release();
		m_pStaticLightBuffer = TNULL;
	}

	m_bPreviousBufferValid = TFALSE;

	if ( g_pLightManager == this )
		g_pLightManager = TNULL;
}

TBOOL LightManager::CreateLightsCBuffer()
{
	if ( m_pLightBuffer )
		return TTRUE;

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth      = sizeof( DynamicLightCBuffer );
	bufferDesc.Usage          = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = g_pRender->GetD3D11Device()->CreateBuffer( &bufferDesc, TNULL, &m_pLightBuffer );
	TASSERT( SUCCEEDED( hr ) );
	return SUCCEEDED( hr );
}

TBOOL LightManager::CreateStaticLightCBuffer()
{
	if ( m_pStaticLightBuffer )
		return TTRUE;

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth      = sizeof( StaticLightCBuffer );
	bufferDesc.Usage          = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = g_pRender->GetD3D11Device()->CreateBuffer( &bufferDesc, TNULL, &m_pStaticLightBuffer );
	TASSERT( SUCCEEDED( hr ) );
	return SUCCEEDED( hr );
}

TBOOL LightManager::CreateShadowResources()
{
	if ( m_pShadowTexture )
		return TTRUE;

	ID3D11Device* pDevice = g_pRender->GetD3D11Device();

	// One depth texture, sliced into an array so every light gets its own shadow map.
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width                = DYNAMIC_LIGHT_SHADOW_RESOLUTION;
	textureDesc.Height               = DYNAMIC_LIGHT_SHADOW_RESOLUTION;
	textureDesc.MipLevels            = 1;
	textureDesc.ArraySize            = DYNAMIC_LIGHT_COUNT;
	textureDesc.Format               = DXGI_FORMAT_R16_TYPELESS;
	textureDesc.SampleDesc.Count     = 1;
	textureDesc.SampleDesc.Quality   = 0;
	textureDesc.Usage                = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	DX11_API_VALIDATE_EXIT( pDevice->CreateTexture2D( &textureDesc, TNULL, &m_pShadowTexture ) );

	for ( TINT i = 0; i < DYNAMIC_LIGHT_COUNT; i++ )
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format                         = DXGI_FORMAT_D16_UNORM;
		dsvDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsvDesc.Texture2DArray.MipSlice        = 0;
		dsvDesc.Texture2DArray.FirstArraySlice = i;
		dsvDesc.Texture2DArray.ArraySize       = 1;

		DX11_API_VALIDATE_EXIT( pDevice->CreateDepthStencilView( m_pShadowTexture, &dsvDesc, &m_apShadowDSV[ i ] ) );
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format                          = DXGI_FORMAT_R16_FLOAT;
	srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip  = 0;
	srvDesc.Texture2DArray.MipLevels        = 1;
	srvDesc.Texture2DArray.FirstArraySlice  = 0;
	srvDesc.Texture2DArray.ArraySize        = DYNAMIC_LIGHT_COUNT;
	DX11_API_VALIDATE_EXIT( pDevice->CreateShaderResourceView( m_pShadowTexture, &srvDesc, &m_pShadowSRV ) );

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter             = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc     = D3D11_COMPARISON_LESS_EQUAL;
	samplerDesc.MinLOD             = 0.0f;
	samplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;
	DX11_API_VALIDATE_EXIT( pDevice->CreateSamplerState( &samplerDesc, &m_pShadowSampler ) );

	return TTRUE;
}

DynamicLightSettings LightManager::GetDynamicLightSettings( Toshi::TLightID a_iLightID ) const
{
	if ( a_iLightID >= 0 && a_iLightID < MAX_DYNAMIC_LIGHT_SETTINGS && m_aDynamicLightSettings[ a_iLightID ].bOverride )
		return m_aDynamicLightSettings[ a_iLightID ];

	// No override for this light -- hand back the current global defaults.
	DynamicLightSettings def     = {};
	def.bOverride                = TFALSE;
	def.flSurfaceIntensity       = g_flDynamicLightIntensity;
	def.flVolumetricIntensity    = g_flDynamicLightVolumetricIntensity;
	def.flColor[ 0 ]             = g_flDynamicLightColor[ 0 ];
	def.flColor[ 1 ]             = g_flDynamicLightColor[ 1 ];
	def.flColor[ 2 ]             = g_flDynamicLightColor[ 2 ];
	def.bFlickerEnabled          = g_bDynamicLightFlickerEnabled;
	def.flFlickerSpeed           = g_flDynamicLightFlickerSpeed;
	def.flFlickerStrength        = g_flDynamicLightFlickerStrength;
	def.flShadowIntensity        = g_flDynamicLightShadowIntensity;
	def.flShadowBias             = g_flDynamicLightShadowBias;
	def.flBumpScale              = g_flDynamicLightBumpScale;
	return def;
}

void LightManager::SetDynamicLightSettings( Toshi::TLightID a_iLightID, const DynamicLightSettings& a_rSettings )
{
	if ( a_iLightID < 0 || a_iLightID >= MAX_DYNAMIC_LIGHT_SETTINGS )
		return;

	m_aDynamicLightSettings[ a_iLightID ]           = a_rSettings;
	m_aDynamicLightSettings[ a_iLightID ].bOverride = TTRUE;
}

void LightManager::ClearDynamicLightSettings( Toshi::TLightID a_iLightID )
{
	if ( a_iLightID < 0 || a_iLightID >= MAX_DYNAMIC_LIGHT_SETTINGS )
		return;

	TUtil::MemClear( &m_aDynamicLightSettings[ a_iLightID ], sizeof( DynamicLightSettings ) );
}

TINT LightManager::FindShadowIndex( Toshi::TLightID a_iLightID ) const
{
	for ( TINT i = 0; i < m_iNumShadowLights; i++ )
	{
		if ( m_aiShadowLightIDs[ i ] == a_iLightID )
			return i;
	}

	return -1;
}

void LightManager::UploadCBufferData( const DynamicLightCBuffer& a_rCBuffer )
{
	if ( !m_pLightBuffer && !CreateLightsCBuffer() )
		return;

	// Only re-map when something actually changed since the last upload.
	if ( !m_bPreviousBufferValid || TUtil::MemCompare( &m_oPreviousBuffer, &a_rCBuffer, sizeof( DynamicLightCBuffer ) ) != 0 )
	{
		m_oPreviousBuffer      = a_rCBuffer;
		m_bPreviousBufferValid = TTRUE;

		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT hMapResult = g_pRender->GetD3D11DeviceContext()->Map( m_pLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
		TASSERT( S_OK == hMapResult );
		if ( S_OK != hMapResult ) return;

		TUtil::MemCopy( mappedResource.pData, &a_rCBuffer, sizeof( a_rCBuffer ) );
		g_pRender->GetD3D11DeviceContext()->Unmap( m_pLightBuffer, 0 );
	}

	g_pRender->PSSetConstantBuffer( 2, m_pLightBuffer );
	g_pRender->PSSetShaderResource( 6, m_pShadowSRV );
	g_pRender->PSSetSamplerState( 6, m_pShadowSampler );
}

void LightManager::RenderDynamicLightShadowMaps()
{
	m_iNumShadowLights = 0;

	if ( !g_bDynamicLightEnabled )
		return;

	AGlowViewport* pGlowViewport = AGlowViewport::GetSingleton();
	if ( !pGlowViewport || !pGlowViewport->m_pHeadUsedObject )
		return;

	if ( !CreateShadowResources() )
		return;

	const TBOOL bRenderShadows = g_bDynamicLightShadowsEnabled && g_pCSMManager;

	// Cull by distance from the gameplay camera (falling back to the render context's eye).
	TVector3 vCameraPos = g_pRender->GetCurrentContext()->GetViewWorldMatrix().GetTranslation3();
	if ( ACameraManager::GetSingleton() )
	{
		ACamera* pCamera = ACameraManager::GetSingleton()->GetCurrentCamera();
		if ( pCamera )
			vCameraPos = pCamera->m_Matrix.GetTranslation3();
	}

	for ( AGlowViewport::GlowObject* pGlowObject = pGlowViewport->m_pHeadUsedObject; pGlowObject != TNULL && m_iNumShadowLights < DYNAMIC_LIGHT_COUNT; pGlowObject = pGlowObject->m_pNextObject )
	{
		if ( !pGlowObject->IsEnabled() )
			continue;

		TMatrix44 oGlowWorld;
		if ( !BuildGlowObjectWorldTransform( oGlowWorld, pGlowObject ) )
			continue;

		const TFLOAT flDistanceSq = TVector3::DistanceSq( oGlowWorld.GetTranslation3(), vCameraPos );
		if ( flDistanceSq > g_flDynamicLightShadowDistance * g_flDynamicLightShadowDistance )
			continue;

		TMatrix44 oLightView;
		oLightView.InvertOrthogonal( oGlowWorld );

		TMatrix44 oLightProjection;
		if ( pGlowObject->m_eCameraMode == TRenderContext::CameraMode_Orthographic )
			TRenderContext::ComputeOrthographicProjection( oLightProjection, pGlowObject->m_oViewportParams, pGlowObject->m_oProjectionParams );
		else
			TRenderContext::ComputePerspectiveProjection( oLightProjection, pGlowObject->m_oViewportParams, pGlowObject->m_oProjectionParams );

		const TINT iShadowIndex = m_iNumShadowLights++;
		m_aiShadowLightIDs[ iShadowIndex ] = pGlowObject->m_iID;
		m_aShadowLightViewProj[ iShadowIndex ].Multiply( oLightProjection, oLightView );

		if ( bRenderShadows )
		{
			g_pCSMManager->RenderCustomShadowMap(
			    oLightView,
			    oLightProjection,
			    pGlowObject->m_oProjectionParams,
			    pGlowObject->m_eCameraMode,
			    m_apShadowDSV[ iShadowIndex ],
			    DYNAMIC_LIGHT_SHADOW_RESOLUTION
			);
		}
	}
}

void LightManager::UploadDynamicLightsCBuffer( Toshi::TRenderPacket* a_pRenderPacket )
{
	DynamicLightCBuffer cbData     = {};
	TINT                iNumLights = 0;

	TLightIDList aLightIDs;
	if ( a_pRenderPacket->m_pUnk )
	{
		aLightIDs = TREINTERPRETCAST( LightDataPacket*, a_pRenderPacket->m_pUnk )->oDynamicLights;
	}
	else
	{
		aLightIDs[ 0 ] = a_pRenderPacket->m_ui8Unk1;
		aLightIDs[ 1 ] = aLightIDs[ 2 ] = aLightIDs[ 3 ] = -1;
	}

	for ( TINT i = 0; i < TLightIDList::MAX_NUM_LIGHTS && iNumLights < DYNAMIC_LIGHT_COUNT; i++ )
	{
		const TLightID iLightID = aLightIDs[ i ];
		if ( iLightID < 0 )
			continue;

		// A per-object light list can name the same light twice; using it for two slots
		// would double its contribution and blow out the mesh, so skip the repeats.
		TBOOL bDuplicate = TFALSE;
		for ( TINT j = 0; j < i; j++ )
		{
			if ( aLightIDs[ j ] == iLightID )
			{
				bDuplicate = TTRUE;
				break;
			}
		}
		if ( bDuplicate )
			continue;

		AGlowViewport::GlowObject*  pGlowObject  = GetGlowObjectByID( iLightID );
		const DynamicLightSettings  settings     = GetDynamicLightSettings( iLightID );
		const TFLOAT                flFlicker    = ComputeFlickerMultiplier( settings );
		const TINT                  iShadowIndex = FindShadowIndex( iLightID );

		if ( FillDynamicLightCBufferEntry( cbData, iNumLights, pGlowObject, TFALSE, settings, flFlicker, iShadowIndex, m_aShadowLightViewProj ) )
			iNumLights++;
	}

	cbData.params.x = g_bDynamicLightEnabled ? TFLOAT( iNumLights ) : 0.0f;

	UploadCBufferData( cbData );
}

void LightManager::UploadVolumetricDynamicLightsCBuffer()
{
	DynamicLightCBuffer cbData     = {};
	TINT                iNumLights = 0;

	for ( TINT i = 0; i < m_iNumShadowLights && iNumLights < DYNAMIC_LIGHT_COUNT; i++ )
	{
		const TLightID              iLightID     = m_aiShadowLightIDs[ i ];
		AGlowViewport::GlowObject*  pGlowObject  = GetGlowObjectByID( iLightID );

		const DynamicLightSettings  settings     = GetDynamicLightSettings( iLightID );
		const TFLOAT                flFlicker    = ComputeFlickerMultiplier( settings );
		const TINT                  iShadowIndex = FindShadowIndex( iLightID );

		if ( FillDynamicLightCBufferEntry( cbData, iNumLights, pGlowObject, TTRUE, settings, flFlicker, iShadowIndex, m_aShadowLightViewProj ) )
			iNumLights++;
	}

	cbData.params.x = g_bDynamicLightEnabled ? TFLOAT( iNumLights ) : 0.0f;

	UploadCBufferData( cbData );
}

//-----------------------------------------------------------------------------
// Static point lights
//-----------------------------------------------------------------------------

// Current night state, read the same way AGlowViewport gates its night lights:
// AGameTimeManager::ms_pInstance @ 0x00783d3c, day phase at +0x34 (4 == night).
static TBOOL IsItNight()
{
	const TCHAR* pGameTimeManager = *TREINTERPRETCAST( const TCHAR**, 0x00783d3c );
	return pGameTimeManager && *TREINTERPRETCAST( const TINT*, pGameTimeManager + 0x34 ) == 4;
}

void LightManager::GetInfluencingStaticLightIDs( const TSphere& a_rcBounds, TLightIDList& a_rOutList ) const
{
	a_rOutList.Reset();

	const TVector3& vBoundsCenter = a_rcBounds.GetOrigin();
	const TFLOAT    fBoundsRadius = a_rcBounds.GetRadius();
	const TBOOL     bIsNight      = IsItNight();

	TINT iNumAdded = 0;
	for ( TINT i = 0; i < m_iNumStaticPointLights && iNumAdded < TLightIDList::MAX_NUM_LIGHTS; i++ )
	{
		const StaticPointLight& light = m_aStaticPointLights[ i ];
		if ( !( light.uiFlags & STATIC_LIGHT_ENABLED ) )
			continue;

		if ( ( light.uiFlags & STATIC_LIGHT_NIGHT_ONLY ) && !bIsNight )
			continue;

		// Sphere overlap: centre distance within the sum of radii.
		const TVector3 vLightCenter( light.vPosition.x, light.vPosition.y, light.vPosition.z );
		const TFLOAT   fReach = fBoundsRadius + light.vPosition.w;

		if ( TVector3::DistanceSq( vBoundsCenter, vLightCenter ) <= fReach * fReach )
		{
			a_rOutList.Add( TLightID( i ) );
			iNumAdded++;
		}
	}
}

TINT LightManager::AddStaticPointLight( const StaticPointLight& a_rLight )
{
	if ( m_iNumStaticPointLights >= MAX_STATIC_POINT_LIGHTS )
		return -1;

	const TINT iIndex = m_iNumStaticPointLights++;
	m_aStaticPointLights[ iIndex ] = a_rLight;
	return iIndex;
}

void LightManager::RemoveStaticPointLight( TINT a_iIndex )
{
	if ( a_iIndex < 0 || a_iIndex >= m_iNumStaticPointLights )
		return;

	// Shift the tail down so the list stays contiguous.
	for ( TINT i = a_iIndex; i < m_iNumStaticPointLights - 1; i++ )
		m_aStaticPointLights[ i ] = m_aStaticPointLights[ i + 1 ];

	m_iNumStaticPointLights--;
}

void LightManager::ClearStaticPointLights()
{
	m_iNumStaticPointLights = 0;
}

void LightManager::UploadStaticLightsGlobalCBuffer()
{
	if ( !m_pStaticLightBuffer && !CreateStaticLightCBuffer() )
		return;

	StaticLightCBuffer cbData = {};

	// Slot i holds light i, so the per-cell index lists line up. Unused slots stay zeroed.
	for ( TINT i = 0; i < m_iNumStaticPointLights; i++ )
	{
		const StaticPointLight& light = m_aStaticPointLights[ i ];
		cbData.positionRadius[ i ] = light.vPosition;
		cbData.colorIntensity[ i ] = light.vColor;
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hMapResult = g_pRender->GetD3D11DeviceContext()->Map( m_pStaticLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	TASSERT( S_OK == hMapResult );
	if ( S_OK != hMapResult ) return;

	TUtil::MemCopy( mappedResource.pData, &cbData, sizeof( cbData ) );
	g_pRender->GetD3D11DeviceContext()->Unmap( m_pStaticLightBuffer, 0 );

	g_pRender->PSSetConstantBuffer( 3, m_pStaticLightBuffer );
}

void LightManager::UploadCellStaticLightIndices( Toshi::TRenderPacket* a_pRenderPacket, TINT a_iVSBaseSlot )
{
	TVector4 vIndices( -1.0f, -1.0f, -1.0f, -1.0f ); // xyzw = up to 4 indices, -1 = empty
	TINT     iCount = 0;

	// A null packet means this draw gets no static lights (e.g. glow/emissive meshes).
	if ( a_pRenderPacket && a_pRenderPacket->m_pUnk )
	{
		const TLightIDList& rStatic = TREINTERPRETCAST( LightDataPacket*, a_pRenderPacket->m_pUnk )->oStaticLights;

		if ( rStatic.aIDs[ 0 ] >= 0 ) { vIndices.x = TFLOAT( rStatic.aIDs[ 0 ] ); iCount = 1; }
		if ( rStatic.aIDs[ 1 ] >= 0 ) { vIndices.y = TFLOAT( rStatic.aIDs[ 1 ] ); iCount = 2; }
		if ( rStatic.aIDs[ 2 ] >= 0 ) { vIndices.z = TFLOAT( rStatic.aIDs[ 2 ] ); iCount = 3; }
		if ( rStatic.aIDs[ 3 ] >= 0 ) { vIndices.w = TFLOAT( rStatic.aIDs[ 3 ] ); iCount = 4; }
	}

	g_pRender->VSBufferSetVec4( a_iVSBaseSlot, vIndices );
	g_pRender->VSBufferSetVec4( a_iVSBaseSlot + 1, TVector4( TFLOAT( iCount ), 0.0f, 0.0f, 0.0f ) );
}

} // namespace remaster
