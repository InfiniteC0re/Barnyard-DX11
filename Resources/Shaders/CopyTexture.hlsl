#include "ScreenSpace.hlsl"

Texture2D    tex        : register( t0 );
SamplerState texSampler : register( s0 );

float4 ps_main( PS_IN i ) : SV_TARGET
{
    return tex.Sample( texSampler, i.UV );
}
