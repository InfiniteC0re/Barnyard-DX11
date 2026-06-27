#include "ScreenSpace.hlsl"

Texture2D    tex     : register( t0 );
SamplerState texSamp : register( s0 );

// Reuses the KawaseCBuffer layout; PADDING carries the bloom threshold here.
cbuffer KawaseCBuffer : register( b1 )
{
    float2 cb_texelSize;
    float  cb_offset;
    float  cb_threshold;
};

// Hue-preserving bright pass: scale by how far the brightest channel exceeds the threshold.
// Subtracting threshold per-channel would bias toward whichever channel was already brightest.
float3 sampleThresholded( float2 uv )
{
    float3 color      = tex.Sample( texSamp, uv ).rgb;
    float  brightness = max( color.r, max( color.g, color.b ) );
    float  weight     = max( brightness - cb_threshold, 0.0 ) / max( brightness, 1e-5 );
    return color * weight;
}

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float2 halfpixel = cb_texelSize * 0.5;
    float2 o         = halfpixel * cb_offset;

    float3 color = sampleThresholded( i.UV ) * 4.0;
    color += sampleThresholded( i.UV + float2( -o.x, -o.y ) );
    color += sampleThresholded( i.UV + float2(  o.x, -o.y ) );
    color += sampleThresholded( i.UV + float2( -o.x,  o.y ) );
    color += sampleThresholded( i.UV + float2(  o.x,  o.y ) );

    return float4( color / 8.0, 1.0 );
}
