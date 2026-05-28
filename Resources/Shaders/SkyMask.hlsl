#include "ScreenSpace.hlsl"

Texture2D    depthbuffer  : register( t0 );
SamplerState depthSampler : register( s0 );

Texture2D    framebuffer  : register( t1 );
SamplerState frameSampler : register( s1 );

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float2 texCoord = i.UV;

    float depth = depthbuffer.SampleLevel( depthSampler, texCoord, 0 ).r;
    clip( depth - 0.99999 );

    float4 skyColor = framebuffer.SampleLevel( frameSampler, texCoord, 0 );
    return skyColor;
}
