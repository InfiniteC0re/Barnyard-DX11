// SMAA 1x, pass 1: luma edge detection. Upstream SMAA library (iryoku/SMAA, MIT). VS offset math is
// folded into the pixel shader because all full-screen passes share one VS (DrawScreenRectangle)

#define SMAA_HLSL_4
#define SMAA_PRESET_HIGH

cbuffer AAParams : register( b1 )
{
    float4 g_RTMetrics; // ( 1/width, 1/height, width, height )
};
#define SMAA_RT_METRICS g_RTMetrics

// SMAA.hlsl declares LinearSampler (s0) / PointSampler (s1); renderer binds SAMPLER_LINEAR_CLAMP /
// SAMPLER_POINT_CLAMP to those slots
#include "ScreenSpace.hlsl"
#include "SMAA.hlsl"

Texture2D colorTex : register( t0 ); // tone-mapped LDR scene (gamma space)

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float4 offset[3];
    SMAAEdgeDetectionVS( i.UV, offset );

    float2 edges = SMAALumaEdgeDetectionPS( i.UV, offset, colorTex );
    return float4( edges, 0.0f, 0.0f );
}
