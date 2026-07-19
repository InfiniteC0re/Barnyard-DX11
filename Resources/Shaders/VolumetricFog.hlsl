// STATIC: "NO_DYN_LIGHT" "0..1"
// STATIC: "CLOUD_SHADOWS" "0..1"
#include "ScreenSpace.hlsl"

Texture2D              depthTexture  : register( t0 );
Texture2DArray         shadowMaps    : register( t1 );
SamplerState           pointSampler  : register( s0 );
SamplerComparisonState shadowSampler : register( s1 );
#if CLOUD_SHADOWS
Texture2D              cloudShadowTex : register( t2 );
SamplerState           cloudSampler  : register( s2 );
#endif
Texture3D              fogNoiseVolume  : register( t3 ); // baked tileable fBm density noise
SamplerState           fogNoiseSampler : register( s3 ); // linear WRAP

#if !NO_DYN_LIGHT
// Single-tap glow-light shadow per march step -- the 3x3 PCF the surface shaders use is wasted
// here because the fog integrates visibility over the whole ray (up to ~176 steps). Cuts the
// per-light per-step shadow cost 9x with no visible change to the volumetrics.
#define GLOW_SHADOW_PCF_RADIUS 0
#include "DynamicLights.hlsli"
#endif

cbuffer VolumetricFogCBuffer : register( b1 )
{
    float4x4 cb_matLightVP[ 3 ];
    float4   cb_CascadeSplits;
    float4   cb_ShadowBias;
    float4x4 cb_matViewWorld;
    float4   cb_Projection;   // m11, m22, m31, m32
    float4   cb_DepthParams;  // unused, unused, near, far
    float4   cb_LightDirVS;   // view-space direction toward sun
    float4   cb_FogColor;
    float4   cb_FogParams;    // density, anisotropy, max distance, intensity/darkening
    float4   cb_FrameParams;  // x = temporal frame index, y = wind time (seconds)
    float4   cb_CloudParams;  // xy = cloud region min (X,Z), z = 1/region size, w = strength (0 = off)
    float4   cb_FogNoiseParams; // x = scale (frequency), y = strength (0 = uniform), zw = wind velocity (world XZ)
    float4   cb_FogHeightParams; // x = bottom height (full at/below), y = top height (0 at/above; <= bottom disables)
};

static const float PI                    = 3.14159265f;
// 0.25 keeps the per-frame march fine enough to look clean on its own. The coarser 0.4 only
// works paired with temporal accumulation (ps_temporal), which is currently dormant because
// the no-reprojection history blend lags the camera -- re-coarsen once reprojection lands.
static const int   MAX_LIGHT_STEPS       = 384;
static const int   MAX_TRANSMITTANCE_STEPS = 64;
static const float LIGHT_STEP_LENGTH     = 0.25f;
static const float TRANSMITTANCE_STEP_LENGTH = 0.5f;
static const float LIGHT_EXTINCTION_SCALE = 0.05f;
static const float SHADOW_EXTINCTION_SCALE = 0.25f;

float LinearizeDepth( float hwDepth )
{
    float invNear = 1.0f / max( cb_DepthParams.z, 0.00001f );
    float invFar  = 1.0f / max( cb_DepthParams.w, cb_DepthParams.z + 0.00001f );
    return 1.0f / ( hwDepth * ( invFar - invNear ) + invNear );
}

float3 UVToViewRay( float2 uv )
{
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    return float3(
        ( ndc.x - cb_Projection.z ) / cb_Projection.x,
        ( ndc.y - cb_Projection.w ) / cb_Projection.y,
        1.0f
    );
}

float HenyeyGreenstein( float cosTheta, float g )
{
    float g2    = g * g;
    float denom = max( 1.0f + g2 - 2.0f * g * cosTheta, 0.0001f );
    return ( 1.0f - g2 ) / ( 4.0f * PI * pow( denom, 1.5f ) );
}

float InterleavedGradientNoise( float2 pixel )
{
    return frac( 52.9829189f * frac( dot( pixel, float2( 0.06711056f, 0.00583715f ) ) ) );
}

float RayJitter( float2 pixel )
{
    float frame = frac( cb_FrameParams.x * ( 1.0f / 8.0f ) ) * 8.0f;
    return InterleavedGradientNoise( pixel + float2( frame * 19.0f, frame * 47.0f ) );
}

// Two-octave density: a single tap slides rigidly with the wind (blobby); a finer second tap
// scrolls differently and is domain-warped by the base so density churns as it advects. Added
// zero-mean to preserve the base mean (~0.5)/range the density mapping expects; non-integer freq
// ratio + WRAP tiling keep the octaves from beating into a visible repeat
float SampleFogNoise( float3 worldPos )
{
    static const float DETAIL_FREQ   = 2.9f;
    static const float DETAIL_WARP   = 0.10f;
    static const float DETAIL_WEIGHT = 0.7f;

    float  scale = cb_FogNoiseParams.x;
    float2 wind  = cb_FogNoiseParams.zw * cb_FrameParams.y;

    float3 pBase = float3( worldPos.x + wind.x, worldPos.y, worldPos.z + wind.y ) * scale;
    float  nBase = fogNoiseVolume.SampleLevel( fogNoiseSampler, pBase, 0 ).r;

    float3 pDetail = float3( worldPos.x - wind.x * 1.7f,
                             worldPos.y + cb_FrameParams.y * 0.6f,
                             worldPos.z - wind.y * 1.7f ) * ( scale * DETAIL_FREQ );
    pDetail += ( nBase - 0.5f ) * DETAIL_WARP;
    float nDetail = fogNoiseVolume.SampleLevel( fogNoiseSampler, pDetail, 0 ).r;

    return saturate( nBase + ( nDetail - 0.5f ) * DETAIL_WEIGHT );
}

// Base fog density modulated by the baked fBm volume (strength 0 = uniform, 1 = 0..2x)
float FogDensityAt( float3 worldPos )
{
    float base = max( cb_FogParams.x, 0.0f );

    // Height band: full at/below bottom, smoothly to 0 at top; top <= bottom disables it
    float fogBottom = cb_FogHeightParams.x;
    float fogTop    = cb_FogHeightParams.y;
    if ( fogTop > fogBottom )
    {
        float h = saturate( ( fogTop - worldPos.y ) / ( fogTop - fogBottom ) );
        base *= h * h * ( 3.0f - 2.0f * h );
    }

    float strength = cb_FogNoiseParams.y;
    if ( strength <= 0.0f )
        return base;

    float n = SampleFogNoise( worldPos );

    return base * lerp( 1.0f, n * 2.0f, strength );
}

float SampleShadow( float3 worldPos, float viewDepth )
{
    int cascade = 2;
    if ( viewDepth < cb_CascadeSplits.x ) cascade = 0;
    else if ( viewDepth < cb_CascadeSplits.y ) cascade = 1;

    float4 shadowPos = mul( float4( worldPos, 1.0f ), cb_matLightVP[ cascade ] );
    shadowPos.xyz /= shadowPos.w;

    float2 cascadeUV = shadowPos.xy * float2( 0.5f, -0.5f ) + 0.5f;
    float  shadowZ   = shadowPos.z - cb_ShadowBias.x;

    if ( any( cascadeUV < 0.0f ) || any( cascadeUV > 1.0f ) ||
         shadowPos.z < 0.0f || shadowPos.z > 1.0f )
        return 1.0f;

    // Per-cascade atlas UV scale packed into cb_ShadowBias.yzw (cascade 0/1/2).
    float scale = ( cascade == 0 ) ? cb_ShadowBias.y : ( ( cascade == 1 ) ? cb_ShadowBias.z : cb_ShadowBias.w );
    float2 shadowUV = cascadeUV * scale;

    return shadowMaps.SampleCmpLevelZero( shadowSampler, float3( shadowUV, (float)cascade ), shadowZ );
}

#if CLOUD_SHADOWS
// Animated cloud shadow, sampled by world XZ -- matches the surface receivers so the
// shafts dim under cloud cover. Outside the camera-centred region => full sun.
float SampleCloudLight( float2 worldXZ )
{
    if ( cb_CloudParams.w <= 0.0f )
        return 1.0f;

    float2 uv = ( worldXZ - cb_CloudParams.xy ) * cb_CloudParams.z;
    if ( any( uv < 0.0f ) || any( uv > 1.0f ) )
        return 1.0f;

    // Fade out over the outer ~15% of the region so the bake edge isn't a hard line.
    float2 d    = min( uv, 1.0f - uv );
    float  edge = smoothstep( 0.0f, 0.15f, min( d.x, d.y ) );

    float sun = cloudShadowTex.SampleLevel( cloudSampler, uv, 0 ).r;
    return lerp( 1.0f, sun, edge * cb_CloudParams.w );
}
#endif // CLOUD_SHADOWS

float GetSceneViewDepth( float2 uv )
{
    float hwDepth = depthTexture.SampleLevel( pointSampler, uv, 0 ).r;
    return ( hwDepth >= 0.99999f ) ? cb_DepthParams.w : LinearizeDepth( hwDepth );
}

float3 GetWorldPosition( float3 rayDirVS, float viewDepth )
{
    return mul( float4( rayDirVS * viewDepth, 1.0f ), cb_matViewWorld ).xyz;
}

float3 IntegrateLightRay( float2 uv, float2 pixel )
{
    float  sceneViewDepth = GetSceneViewDepth( uv );
    float3 rayDirVS       = UVToViewRay( uv );
    float3 rayToSceneVS   = rayDirVS * sceneViewDepth;
    float  rayDistance    = min( length( rayToSceneVS ), cb_FogParams.z );
    if ( rayDistance <= 0.0001f )
        return 0.0f;

    float3 marchDirVS = rayToSceneVS / max( length( rayToSceneVS ), 0.0001f );
    float  g        = clamp( cb_FogParams.y, -0.95f, 0.95f );

    int   stepCount = min( MAX_LIGHT_STEPS, max( 8, (int)ceil( rayDistance / LIGHT_STEP_LENGTH ) ) );
    float stepSize  = rayDistance / (float)stepCount;
    float jitter    = RayJitter( pixel );

    // Keep the scattering phase tied to the camera orientation. Using each pixel's
    // view ray makes the same world-space shaft fade as it moves toward screen edges.
    float3 cameraForwardVS = normalize( UVToViewRay( float2( 0.5f, 0.5f ) ) );
    float cosTheta = dot( cameraForwardVS, normalize( cb_LightDirVS.xyz ) );
    float phase    = HenyeyGreenstein( cosTheta, g ) * ( 4.0f * PI );

    float  transmittance = 1.0f;
    float3 radiance      = 0.0f;

    // Skip steps under 5% of base density -- a noise gap costs a shadow + light sample for ~0. With
    // noise off density == base, so never skips
    float minDensity = max( cb_FogParams.x, 0.0f ) * 0.05f;

    [loop]
    for ( int step = 0; step < MAX_LIGHT_STEPS; step++ )
    {
        if ( step >= stepCount ) break;

        float rayT = ( (float)step + jitter ) * stepSize;
        float3 viewPos = marchDirVS * rayT;
        float3 worldPos = mul( float4( viewPos, 1.0f ), cb_matViewWorld ).xyz;

        float density = FogDensityAt( worldPos );
        if ( density < minDensity )
            continue;

        float extinction = density * LIGHT_EXTINCTION_SCALE;
        float scattering = density;
#if CLOUD_SHADOWS
        float visibility = SampleShadow( worldPos, viewPos.z ) * SampleCloudLight( worldPos.xz );
#else
        float visibility = SampleShadow( worldPos, viewPos.z );
#endif

        float stepTransmittance = exp( -extinction * stepSize );
        float stepScatter       = scattering * stepSize;

        if ( extinction > 0.00001f )
            stepScatter = ( scattering / extinction ) * ( 1.0f - stepTransmittance );

        float3 stepLighting = visibility * phase * cb_FogColor.rgb * cb_FogParams.w;
#if !NO_DYN_LIGHT
        stepLighting += SampleDynamicGlowLights( worldPos );
#endif

        radiance      += transmittance * stepLighting * stepScatter;
        transmittance *= stepTransmittance;

        if ( transmittance < 0.001f ) break;
    }

    return radiance;
}

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float3 color = IntegrateLightRay( i.UV, i.Position.xy );
    return float4( color, 1.0f );
}

float4 ps_visibility( PS_IN i ) : SV_TARGET
{
    float  sceneViewDepth = GetSceneViewDepth( i.UV );
    float3 rayDirVS       = UVToViewRay( i.UV );
    float3 rayToSceneVS   = rayDirVS * sceneViewDepth;
    float  rayDistance    = min( length( rayToSceneVS ), cb_FogParams.z );
    if ( rayDistance <= 0.0001f )
        return float4( 1.0f, 1.0f, 1.0f, 1.0f );

    float3 marchDirVS = rayToSceneVS / max( length( rayToSceneVS ), 0.0001f );
    float  amount   = saturate( cb_FogParams.w );

    int   stepCount = min( MAX_TRANSMITTANCE_STEPS, max( 8, (int)ceil( rayDistance / TRANSMITTANCE_STEP_LENGTH ) ) );
    float stepSize  = rayDistance / (float)stepCount;
    float jitter    = RayJitter( i.Position.xy );

    float shadowedOpticalDepth = 0.0f;

    float minDensity = max( cb_FogParams.x, 0.0f ) * 0.05f;

    [loop]
    for ( int step = 0; step < MAX_TRANSMITTANCE_STEPS; step++ )
    {
        if ( step >= stepCount ) break;

        float rayT = ( (float)step + jitter ) * stepSize;
        float3 viewPos = marchDirVS * rayT;
        float3 worldPos = mul( float4( viewPos, 1.0f ), cb_matViewWorld ).xyz;

        float density = FogDensityAt( worldPos );
        if ( density < minDensity )
            continue;

#if CLOUD_SHADOWS
        float visibility = SampleShadow( worldPos, viewPos.z ) * SampleCloudLight( worldPos.xz );
#else
        float visibility = SampleShadow( worldPos, viewPos.z );
#endif

        shadowedOpticalDepth += ( 1.0f - visibility ) * density * SHADOW_EXTINCTION_SCALE * stepSize;
    }

    float transmittance = exp( -shadowedOpticalDepth * amount );
    return float4( transmittance, transmittance, transmittance, 1.0f );
}
