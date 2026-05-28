#include "ScreenSpace.hlsl"

Texture2D    aoTexture : register( t0 );
Texture2D    sceneTexture : register( t1 );
SamplerState aoSampler : register( s0 );
SamplerState sceneSampler : register( s1 );

float4 ps_debug( PS_IN i ) : SV_TARGET
{
    float ao = aoTexture.SampleLevel( aoSampler, i.UV, 0 ).r;
    return float4( ao, ao, ao, 1.0f );
}

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float ao = aoTexture.SampleLevel( aoSampler, i.UV, 0 ).r;
    float4 scene = sceneTexture.SampleLevel( sceneSampler, i.UV, 0 );
    return float4( scene.rgb * ao, scene.a );
}
