// STATIC: "NO_DYN_LIGHT" "0..1"
#include "ScreenSpace.hlsl"

Texture2D              depthTexture  : register( t0 );
Texture2DArray         shadowMaps    : register( t1 );
SamplerState           pointSampler  : register( s0 );
SamplerComparisonState shadowSampler : register( s1 );

#if !NO_DYN_LIGHT
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
    float4   cb_FrameParams;  // temporal frame index
};

static const float PI                    = 3.14159265f;
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
    float  density  = max( cb_FogParams.x, 0.0f );
    float  g        = clamp( cb_FogParams.y, -0.95f, 0.95f );

    int   stepCount = min( MAX_LIGHT_STEPS, max( 8, (int)ceil( rayDistance / LIGHT_STEP_LENGTH ) ) );
    float stepSize  = rayDistance / (float)stepCount;
    float jitter    = RayJitter( pixel );

    // Keep the scattering phase tied to the camera orientation. Using each pixel's
    // view ray makes the same world-space shaft fade as it moves toward screen edges.
    float3 cameraForwardVS = normalize( UVToViewRay( float2( 0.5f, 0.5f ) ) );
    float cosTheta = dot( cameraForwardVS, normalize( cb_LightDirVS.xyz ) );
    float phase    = HenyeyGreenstein( cosTheta, g ) * ( 4.0f * PI );

    float  extinction    = density * LIGHT_EXTINCTION_SCALE;
    float  scattering    = density;
    float  transmittance = 1.0f;
    float3 radiance      = 0.0f;

    [loop]
    for ( int step = 0; step < MAX_LIGHT_STEPS; step++ )
    {
        if ( step >= stepCount ) break;

        float rayT = ( (float)step + jitter ) * stepSize;
        float3 viewPos = marchDirVS * rayT;
        float3 worldPos = mul( float4( viewPos, 1.0f ), cb_matViewWorld ).xyz;
        float visibility = SampleShadow( worldPos, viewPos.z );

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
    float  density  = max( cb_FogParams.x, 0.0f );
    float  amount   = saturate( cb_FogParams.w );

    int   stepCount = min( MAX_TRANSMITTANCE_STEPS, max( 8, (int)ceil( rayDistance / TRANSMITTANCE_STEP_LENGTH ) ) );
    float stepSize  = rayDistance / (float)stepCount;
    float jitter    = RayJitter( i.Position.xy );

    float shadowedOpticalDepth = 0.0f;

    [loop]
    for ( int step = 0; step < MAX_TRANSMITTANCE_STEPS; step++ )
    {
        if ( step >= stepCount ) break;

        float rayT = ( (float)step + jitter ) * stepSize;
        float3 viewPos = marchDirVS * rayT;
        float3 worldPos = mul( float4( viewPos, 1.0f ), cb_matViewWorld ).xyz;
        float visibility = SampleShadow( worldPos, viewPos.z );

        shadowedOpticalDepth += ( 1.0f - visibility ) * density * SHADOW_EXTINCTION_SCALE * stepSize;
    }

    float transmittance = exp( -shadowedOpticalDepth * amount );
    return float4( transmittance, transmittance, transmittance, 1.0f );
}
