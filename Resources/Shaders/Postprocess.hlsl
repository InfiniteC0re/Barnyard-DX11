#include "ScreenSpace.hlsl"

Texture2D    tex        : register( t0 );
SamplerState texSampler : register( s0 );

float Hash( float2 p )
{
    return frac( 52.9829189f * frac( dot( p, float2( 0.06711056f, 0.00583715f ) ) ) );
}

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float3 color = saturate( tex.Sample( texSampler, i.UV ).rgb );

    // Triangular-PDF dither (~1 LSB at 8-bit). Two hashed uniforms subtracted give a
    // triangular distribution, which masks both the final 8-bit quantization and the banding
    // from the HDR buffer's reduced mantissa (R11G11B10) in smooth sky/fog gradients.
    float2 p      = i.Position.xy;
    float3 dither = ( Hash( p ) + Hash( p + 17.0f ) - 1.0f ) / 255.0f;

    return float4( saturate( color + dither ), 1.0f );
}
