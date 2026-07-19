#include "ScreenSpace.hlsl"
#include "ShaderUtils.hlsli"

Texture2D    tex        : register( t0 );
Texture2D    sceneTex   : register( t1 ); // resolved scene, bound only for ps_composite
SamplerState texSampler : register( s0 );

float4 ps_main( PS_IN i ) : SV_TARGET
{
    return tex.Sample( texSampler, i.UV );
}

// Additive (ONE/ONE) sun-shaft composite, dithered against R11G11B10 banding. Dither is sized to
// the quantisation of scene+shaft (hence reading the scene, t1) -- sizing to the shaft's own step
// lets the sum snap back into bands -- and gated by shaft contribution so untouched pixels stay clean
float4 ps_composite( PS_IN i ) : SV_TARGET
{
    float3 fx    = tex.Sample( texSampler, i.UV ).rgb;
    float3 scene = sceneTex.Sample( texSampler, i.UV ).rgb;

    float3 qStep = exp2( floor( log2( max( scene + fx, 1e-6f ) ) ) - float3( 6.0f, 6.0f, 5.0f ) );
    float  fxMax = max( fx.r, max( fx.g, fx.b ) );
    float  w     = saturate( fxMax / max( qStep.g, 1e-6f ) );

    float  tri   = DitherHash( i.Position.xy ) + DitherHash( i.Position.xy + 17.0f ) - 1.0f;
    return float4( fx + tri * qStep * w, 1.0f );
}
