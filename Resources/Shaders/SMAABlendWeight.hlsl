// SMAA 1x, pass 2: blend-weight calculation. See SMAAEdgeDetection.hlsl for why the SMAA vertex
// offsets are computed in the pixel shader

#define SMAA_HLSL_4
#define SMAA_PRESET_HIGH

cbuffer AAParams : register( b1 )
{
    float4 g_RTMetrics; // ( 1/width, 1/height, width, height )
};
#define SMAA_RT_METRICS g_RTMetrics

// SMAA.hlsl declares its own LinearSampler (s0) / PointSampler (s1)
#include "ScreenSpace.hlsl"
#include "SMAA.hlsl"

Texture2D edgesTex  : register( t0 );
Texture2D areaTex   : register( t1 );
Texture2D searchTex : register( t2 );

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float2 pixcoord;
    float4 offset[3];
    SMAABlendingWeightCalculationVS( i.UV, pixcoord, offset );

    // float4(0) = subsample indices; SMAA 1x has no temporal subsamples
    return SMAABlendingWeightCalculationPS( i.UV, pixcoord, offset, edgesTex, areaTex, searchTex, float4( 0.0f, 0.0f, 0.0f, 0.0f ) );
}
