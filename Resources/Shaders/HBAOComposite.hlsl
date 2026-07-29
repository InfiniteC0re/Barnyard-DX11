#include "ScreenSpace.hlsl"
#include "ShaderUtils.hlsli"

Texture2D    aoTexture : register( t0 );
Texture2D    sceneTexture : register( t1 );
Texture2D    depthTexture : register( t2 );
Texture2D    aoDepthTexture : register( t3 );
SamplerState aoSampler : register( s0 );
SamplerState sceneSampler : register( s1 );
SamplerState pointSampler : register( s2 );

cbuffer HBAOCompositeCBuffer : register( b1 )
{
    float4 cb_DepthParams;  // near, far, unused, unused
    float4 cb_AOBufferSize; // width, height, invWidth, invHeight
};

// Relative depth deviation at which a tap stops contributing. Relative, not absolute, so one
// value holds at every view distance
static const float UPSAMPLE_DEPTH_SIGMA = 0.0075f;

float LinearizeDepth( float hardwareDepth )
{
    float invNear = 1.0f / max( cb_DepthParams.x, 0.00001f );
    float invFar = 1.0f / max( cb_DepthParams.y, cb_DepthParams.x + 0.00001f );
    return 1.0f / ( hardwareDepth * ( invFar - invNear ) + invNear );
}

// Depth-aware upsample of the half-res AO. Plain bilinear drags occlusion across silhouettes
// (halos on foliage and against sky), so scale each bilinear weight by how well that tap's depth
// matches this pixel. Two Gathers cover the same 2x2 the hardware filter would have used, so this
// costs 3 fetches rather than the 9 a 3x3 kernel would
float UpsampleAO( float2 a_vUV, float a_fCenterHardwareDepth )
{
    float4 ao   = aoTexture.GatherRed( aoSampler, a_vUV );
    float4 taps = aoDepthTexture.GatherRed( aoSampler, a_vUV );

    // Gather order is (0,1) (1,1) (1,0) (0,0) relative to the top-left texel of the quad
    float2 f = frac( a_vUV * cb_AOBufferSize.xy - 0.5f );
    float4 bilinear;
    bilinear.x = ( 1.0f - f.x ) * f.y;
    bilinear.y = f.x * f.y;
    bilinear.z = f.x * ( 1.0f - f.y );
    bilinear.w = ( 1.0f - f.x ) * ( 1.0f - f.y );

    float  centerZ = LinearizeDepth( a_fCenterHardwareDepth );
    float4 tapZ    = float4( LinearizeDepth( taps.x ), LinearizeDepth( taps.y ), LinearizeDepth( taps.z ), LinearizeDepth( taps.w ) );
    float4 alpha   = abs( tapZ - centerZ ) / max( centerZ, 0.00001f );
    float4 weight  = bilinear * exp( -( alpha * alpha ) / ( 2.0f * UPSAMPLE_DEPTH_SIGMA * UPSAMPLE_DEPTH_SIGMA ) );

    float total = dot( weight, float4( 1.0f, 1.0f, 1.0f, 1.0f ) );

    // All four taps rejected -- the quad straddles a silhouette with nothing at this depth in it.
    // Bilinear weights already sum to 1, so falling back to them needs no extra fetch
    if ( total < 0.0001f )
    {
        weight = bilinear;
        total  = 1.0f;
    }

    return dot( ao, weight ) / total;
}

float SampleAO( float2 a_vUV )
{
    // The blur smears AO into the pixels bordering the sky even though the AO pass wrote 1.0 there
    float hardwareDepth = depthTexture.SampleLevel( pointSampler, a_vUV, 0 ).r;
    if ( hardwareDepth >= 0.99999f )
        return 1.0f;

    return UpsampleAO( a_vUV, hardwareDepth );
}

float4 ps_debug( PS_IN i ) : SV_TARGET
{
    float ao = SampleAO( i.UV );
    return float4( ao, ao, ao, 1.0f );
}

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float ao = SampleAO( i.UV );
    float4 scene = sceneTexture.SampleLevel( sceneSampler, i.UV, 0 );
    return float4( DitherR11G11B10( scene.rgb * ao, i.Position.xy ), scene.a );
}
