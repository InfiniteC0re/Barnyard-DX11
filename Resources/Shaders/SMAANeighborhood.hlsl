// SMAA 1x, pass 3: neighborhood blending. See SMAAEdgeDetection.hlsl for the in-pixel-shader offsets

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

Texture2D colorTex : register( t0 ); // tone-mapped LDR scene
Texture2D blendTex : register( t1 ); // blend weights from pass 2

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float4 offset;
    SMAANeighborhoodBlendingVS( i.UV, offset );

    return SMAANeighborhoodBlendingPS( i.UV, offset, colorTex, blendTex );
}
