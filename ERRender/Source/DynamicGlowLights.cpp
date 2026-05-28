#include "pch.h"
#include "DynamicGlowLights.h"
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

struct DynamicGlowLightCBuffer
{
	Toshi::TVector4  lightPositionRadius[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ]; // xyz = world pos, w = radius
	Toshi::TVector4  lightDirectionCone[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ];  // xyz = direction, w = cosOuter
	Toshi::TMatrix44 matLightVP[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ];
	Toshi::TVector4  shadowParams[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ];        // x = slice, y = texel size, z = bias, w = shadow strength
	Toshi::TVector4  lightColor[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ];          // xyz = RGB, w = volumetric intensity
	Toshi::TVector4  lightIntensity[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ];      // x = surface intensity, y = bump scale, z = cosInner
	Toshi::TVector4  params;                                                     // x = count
};

static ID3D11Texture2D*          s_pDynamicGlowShadowTexture = TNULL;
static ID3D11DepthStencilView*   s_apDynamicGlowShadowDSV[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ] = {};
static ID3D11ShaderResourceView* s_pDynamicGlowShadowSRV     = TNULL;
static ID3D11SamplerState*       s_pDynamicGlowShadowSampler = TNULL;
static Toshi::TLightID           s_aiShadowLightIDs[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ] = {};
static Toshi::TMatrix44          s_aShadowLightViewProj[ remaster::DYNAMIC_GLOW_LIGHT_COUNT ];
static TINT                      s_iNumShadowLights = 0;

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

	// Editor-placed lights have no scene object -- m_oTransform is already the world transform
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

TBOOL  g_bDynamicGlowEnabled              = TTRUE;
TFLOAT g_flDynamicGlowIntensity           = 1.75f;
TFLOAT g_flDynamicGlowVolumetricIntensity = 0.3f;
TFLOAT g_flDynamicGlowColor[ 3 ]          = { 255.0f / 255.0f, 181.0f / 255.0f, 110.0f / 255.0f };
TBOOL  g_bDynamicGlowShadowsEnabled   = TTRUE;
TFLOAT g_flDynamicGlowShadowDistance  = 45.0f;
TFLOAT g_flDynamicGlowShadowIntensity = 1.0f;
TFLOAT g_flDynamicGlowShadowBias      = 0.0015f;
TFLOAT g_flDynamicGlowBumpScale       = 3.5f;
TBOOL  g_bDynamicGlowFlickerEnabled   = TTRUE;
TFLOAT g_flDynamicGlowFlickerSpeed    = 5.0f;
TFLOAT g_flDynamicGlowFlickerStrength = 0.1f;

// Per-light overrides -- zero-initialised so bOverride starts TFALSE for all slots.
GlowLightSettings g_aGlowLightSettings[ MAX_GLOW_LIGHT_SETTINGS ] = {};

GlowLightSettings GetGlowLightSettings( Toshi::TLightID a_iLightID )
{
	if ( a_iLightID >= 0 && a_iLightID < MAX_GLOW_LIGHT_SETTINGS && g_aGlowLightSettings[ a_iLightID ].bOverride )
		return g_aGlowLightSettings[ a_iLightID ];

	// Populate a transient struct from the current global defaults.
	GlowLightSettings def        = {};
	def.bOverride                = TFALSE;
	def.flSurfaceIntensity       = g_flDynamicGlowIntensity;
	def.flVolumetricIntensity    = g_flDynamicGlowVolumetricIntensity;
	def.flColor[ 0 ]             = g_flDynamicGlowColor[ 0 ];
	def.flColor[ 1 ]             = g_flDynamicGlowColor[ 1 ];
	def.flColor[ 2 ]             = g_flDynamicGlowColor[ 2 ];
	def.bFlickerEnabled          = g_bDynamicGlowFlickerEnabled;
	def.flFlickerSpeed           = g_flDynamicGlowFlickerSpeed;
	def.flFlickerStrength        = g_flDynamicGlowFlickerStrength;
	def.flShadowIntensity        = g_flDynamicGlowShadowIntensity;
	def.flShadowBias             = g_flDynamicGlowShadowBias;
	def.flBumpScale              = g_flDynamicGlowBumpScale;
	return def;
}

// Computes an intensity multiplier in the range [1-strength, 1+strength] using
// three incommensurable sine waves so the pattern never feels mechanical.
// Uses the resolved settings so each light can have independent flicker behaviour.
static TFLOAT ComputeFlickerMultiplier( const GlowLightSettings& a_rSettings )
{
	if ( !a_rSettings.bFlickerEnabled )
		return 1.0f;

	TSystemManager* pSystemManager = TREINTERPRETCAST( TSystemManager*, 0x007ce640 );
	const TFLOAT    fTime          = pSystemManager->GetScheduler()->GetTotalTime();
	const TFLOAT    fS             = fTime * a_rSettings.flFlickerSpeed;

	// Three overlapping sine waves at incommensurable frequencies -- primary swell,
	// mid flutter, and high-frequency crackle.
	const TFLOAT fFlicker =
	    TMath::Sin( fS * 1.00f ) * 0.50f +
	    TMath::Sin( fS * 2.71f ) * 0.30f +
	    TMath::Sin( fS * 7.33f ) * 0.20f;

	return TMath::Max( 0.0f, 1.0f + fFlicker * a_rSettings.flFlickerStrength );
}

static TINT FindDynamicGlowShadowIndex( Toshi::TLightID a_iLightID )
{
	for ( TINT i = 0; i < s_iNumShadowLights; i++ )
	{
		if ( s_aiShadowLightIDs[ i ] == a_iLightID )
			return i;
	}

	return -1;
}

static TBOOL FillDynamicGlowLightCBufferEntry(
    DynamicGlowLightCBuffer&       a_rCBuffer,
    TINT                           a_iLightIndex,
    AGlowViewport::GlowObject*     a_pGlowObject,
    Toshi::TLightID                a_iLightID,
    TBOOL                          a_bRequireShadow,
    const GlowLightSettings&       a_rSettings,
    TFLOAT                         a_flFlicker
)
{
	TMatrix44 oGlowTransform;
	if ( !a_pGlowObject || !a_pGlowObject->IsEnabled() || !BuildGlowObjectWorldTransform( oGlowTransform, a_pGlowObject ) )
		return TFALSE;

	const TINT iShadowIndex = FindDynamicGlowShadowIndex( a_iLightID );
	if ( a_bRequireShadow && iShadowIndex < 0 )
		return TFALSE;

	const TFLOAT flRadius   = TMath::Max( a_pGlowObject->m_oProjectionParams.m_fFarClip, 1.0f );
	TVector4     vDirection = oGlowTransform.AsBasisVector4( BASISVECTOR_FORWARD );
	vDirection.Normalise();

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

	// Per-light colour + intensity (flicker-scaled).
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

	if ( iShadowIndex >= 0 )
	{
		a_rCBuffer.matLightVP[ a_iLightIndex ] = s_aShadowLightViewProj[ iShadowIndex ];
		a_rCBuffer.shadowParams[ a_iLightIndex ] = TVector4(
		    TFLOAT( iShadowIndex ),
		    1.0f / TFLOAT( DYNAMIC_GLOW_SHADOW_RESOLUTION ),
		    a_rSettings.flShadowBias,
		    a_rSettings.flShadowIntensity
		);
	}
	else
	{
		a_rCBuffer.shadowParams[ a_iLightIndex ] = TVector4( -1.0f, 0.0f, 0.0f, 0.0f );
	}

	return TTRUE;
}

static void UploadDynamicGlowLightsCBufferData( const DynamicGlowLightCBuffer& a_rCBuffer, ID3D11Buffer* a_pBuffer )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hMapResult = g_pRender->GetD3D11DeviceContext()->Map( a_pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	TASSERT( S_OK == hMapResult );
	if ( S_OK != hMapResult )
		return;

	TUtil::MemCopy( mappedResource.pData, &a_rCBuffer, sizeof( a_rCBuffer ) );
	g_pRender->GetD3D11DeviceContext()->Unmap( a_pBuffer, 0 );
	g_pRender->PSSetConstantBuffer( 2, a_pBuffer );
	g_pRender->PSSetShaderResource( 6, s_pDynamicGlowShadowSRV );
	g_pRender->PSSetSamplerState( 6, s_pDynamicGlowShadowSampler );
}

TBOOL CreateDynamicGlowLightsCBuffer( ID3D11Buffer** a_ppBuffer )
{
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth      = sizeof( DynamicGlowLightCBuffer );
	bufferDesc.Usage          = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = g_pRender->GetD3D11Device()->CreateBuffer( &bufferDesc, TNULL, a_ppBuffer );
	TASSERT( SUCCEEDED( hr ) );
	return SUCCEEDED( hr );
}

TBOOL CreateDynamicGlowShadowResources()
{
	if ( s_pDynamicGlowShadowTexture )
		return TTRUE;

	ID3D11Device* pDevice = g_pRender->GetD3D11Device();

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width                = DYNAMIC_GLOW_SHADOW_RESOLUTION;
	textureDesc.Height               = DYNAMIC_GLOW_SHADOW_RESOLUTION;
	textureDesc.MipLevels            = 1;
	textureDesc.ArraySize            = DYNAMIC_GLOW_LIGHT_COUNT;
	textureDesc.Format               = DXGI_FORMAT_R32_TYPELESS;
	textureDesc.SampleDesc.Count     = 1;
	textureDesc.SampleDesc.Quality   = 0;
	textureDesc.Usage                = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	DX11_API_VALIDATE_EXIT( pDevice->CreateTexture2D( &textureDesc, TNULL, &s_pDynamicGlowShadowTexture ) );

	for ( TINT i = 0; i < DYNAMIC_GLOW_LIGHT_COUNT; i++ )
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		dsvDesc.Texture2DArray.MipSlice        = 0;
		dsvDesc.Texture2DArray.FirstArraySlice = i;
		dsvDesc.Texture2DArray.ArraySize       = 1;

		DX11_API_VALIDATE_EXIT( pDevice->CreateDepthStencilView( s_pDynamicGlowShadowTexture, &dsvDesc, &s_apDynamicGlowShadowDSV[ i ] ) );
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format                          = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip  = 0;
	srvDesc.Texture2DArray.MipLevels        = 1;
	srvDesc.Texture2DArray.FirstArraySlice  = 0;
	srvDesc.Texture2DArray.ArraySize        = DYNAMIC_GLOW_LIGHT_COUNT;
	DX11_API_VALIDATE_EXIT( pDevice->CreateShaderResourceView( s_pDynamicGlowShadowTexture, &srvDesc, &s_pDynamicGlowShadowSRV ) );

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter             = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc     = D3D11_COMPARISON_LESS_EQUAL;
	samplerDesc.MinLOD             = 0.0f;
	samplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;
	DX11_API_VALIDATE_EXIT( pDevice->CreateSamplerState( &samplerDesc, &s_pDynamicGlowShadowSampler ) );

	return TTRUE;
}

void DestroyDynamicGlowShadowResources()
{
	for ( TINT i = 0; i < DYNAMIC_GLOW_LIGHT_COUNT; i++ )
	{
		if ( s_apDynamicGlowShadowDSV[ i ] )
		{
			s_apDynamicGlowShadowDSV[ i ]->Release();
			s_apDynamicGlowShadowDSV[ i ] = TNULL;
		}
	}

	if ( s_pDynamicGlowShadowSampler )
	{
		s_pDynamicGlowShadowSampler->Release();
		s_pDynamicGlowShadowSampler = TNULL;
	}

	if ( s_pDynamicGlowShadowSRV )
	{
		s_pDynamicGlowShadowSRV->Release();
		s_pDynamicGlowShadowSRV = TNULL;
	}

	if ( s_pDynamicGlowShadowTexture )
	{
		s_pDynamicGlowShadowTexture->Release();
		s_pDynamicGlowShadowTexture = TNULL;
	}
}

void RenderDynamicGlowShadowMaps()
{
	s_iNumShadowLights = 0;

	if ( !g_bDynamicGlowEnabled || !g_bDynamicGlowShadowsEnabled || !g_pCSMManager )
		return;

	AGlowViewport* pGlowViewport = AGlowViewport::GetSingleton();
	if ( !pGlowViewport || !pGlowViewport->m_pHeadUsedObject )
		return;

	if ( !CreateDynamicGlowShadowResources() )
		return;

	TVector3 vCameraPos = g_pRender->GetCurrentContext()->GetViewWorldMatrix().GetTranslation3();
	if ( ACameraManager::GetSingleton() )
	{
		ACamera* pCamera = ACameraManager::GetSingleton()->GetCurrentCamera();
		if ( pCamera )
			vCameraPos = pCamera->m_Matrix.GetTranslation3();
	}

	for ( AGlowViewport::GlowObject* pGlowObject = pGlowViewport->m_pHeadUsedObject; pGlowObject != TNULL && s_iNumShadowLights < DYNAMIC_GLOW_LIGHT_COUNT; pGlowObject = pGlowObject->m_pNextObject )
	{
		if ( !pGlowObject->IsEnabled() )
			continue;

		TMatrix44 oGlowWorld;
		if ( !BuildGlowObjectWorldTransform( oGlowWorld, pGlowObject ) )
			continue;

		const TFLOAT flDistanceSq = TVector3::DistanceSq( oGlowWorld.GetTranslation3(), vCameraPos );
		if ( flDistanceSq > g_flDynamicGlowShadowDistance * g_flDynamicGlowShadowDistance )
			continue;

		TMatrix44 oLightView;
		oLightView.InvertOrthogonal( oGlowWorld );

		TMatrix44 oLightProjection;
		if ( pGlowObject->m_eCameraMode == TRenderContext::CameraMode_Orthographic )
			TRenderContext::ComputeOrthographicProjection( oLightProjection, pGlowObject->m_oViewportParams, pGlowObject->m_oProjectionParams );
		else
			TRenderContext::ComputePerspectiveProjection( oLightProjection, pGlowObject->m_oViewportParams, pGlowObject->m_oProjectionParams );

		const TINT iShadowIndex = s_iNumShadowLights++;
		s_aiShadowLightIDs[ iShadowIndex ] = pGlowObject->m_iID;
		s_aShadowLightViewProj[ iShadowIndex ].Multiply( oLightProjection, oLightView );

		g_pCSMManager->RenderCustomShadowMap(
		    oLightView,
		    oLightProjection,
		    pGlowObject->m_oProjectionParams,
		    pGlowObject->m_eCameraMode,
		    s_apDynamicGlowShadowDSV[ iShadowIndex ],
		    DYNAMIC_GLOW_SHADOW_RESOLUTION
		);
	}
}

void UploadDynamicGlowLightsCBuffer( Toshi::TRenderPacket* a_pRenderPacket, ID3D11Buffer* a_pBuffer )
{
	if ( !a_pBuffer )
		return;

	DynamicGlowLightCBuffer cbData    = {};
	TINT                    iNumLights = 0;

	// Unpack all light IDs stored in m_pUnk (up to 4, invalid slots are -1).
	// Fall back to m_ui8Unk1 alone if m_pUnk was never written (e.g. old code paths).
	TLightID aLightIDs[ TLightIDList::MAX_NUM_LIGHTS ];
	if ( a_pRenderPacket->m_pUnk )
	{
		UnpackRenderPacketLights( a_pRenderPacket->m_pUnk, aLightIDs );
	}
	else
	{
		aLightIDs[ 0 ] = a_pRenderPacket->m_ui8Unk1;
		aLightIDs[ 1 ] = aLightIDs[ 2 ] = aLightIDs[ 3 ] = -1;
	}

	for ( TINT i = 0; i < TLightIDList::MAX_NUM_LIGHTS && iNumLights < DYNAMIC_GLOW_LIGHT_COUNT; i++ )
	{
		const TLightID iLightID = aLightIDs[ i ];
		if ( iLightID < 0 )
			continue;

		AGlowViewport::GlowObject* pGlowObject = GetGlowObjectByID( iLightID );
		const GlowLightSettings    settings    = GetGlowLightSettings( iLightID );
		const TFLOAT               flFlicker   = ComputeFlickerMultiplier( settings );

		if ( FillDynamicGlowLightCBufferEntry( cbData, iNumLights, pGlowObject, iLightID, TFALSE, settings, flFlicker ) )
			iNumLights++;
	}

	cbData.params.x = g_bDynamicGlowEnabled ? TFLOAT( iNumLights ) : 0.0f;

	UploadDynamicGlowLightsCBufferData( cbData, a_pBuffer );
}

void UploadVolumetricDynamicGlowLightsCBuffer( ID3D11Buffer* a_pBuffer )
{
	if ( !a_pBuffer )
		return;

	DynamicGlowLightCBuffer cbData    = {};
	TINT                    iNumLights = 0;

	for ( TINT i = 0; i < s_iNumShadowLights && iNumLights < DYNAMIC_GLOW_LIGHT_COUNT; i++ )
	{
		const TLightID             iLightID    = s_aiShadowLightIDs[ i ];
		AGlowViewport::GlowObject* pGlowObject = GetGlowObjectByID( iLightID );

		const GlowLightSettings settings = GetGlowLightSettings( iLightID );
		const TFLOAT            flFlicker = ComputeFlickerMultiplier( settings );

		if ( FillDynamicGlowLightCBufferEntry( cbData, iNumLights, pGlowObject, iLightID, TTRUE, settings, flFlicker ) )
			iNumLights++;
	}

	cbData.params.x = ( g_bDynamicGlowEnabled && g_bDynamicGlowShadowsEnabled ) ? TFLOAT( iNumLights ) : 0.0f;

	UploadDynamicGlowLightsCBufferData( cbData, a_pBuffer );
}

} // namespace remaster
