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

// AA variant of ps_main: writes the LDR intermediate that feeds the post AA pass (FXAA/SMAA), and
// packs perceptual luma into alpha for FXAA (FxaaLuma). SMAA derives luma itself and ignores alpha
float4 ps_main_aa( PS_IN i ) : SV_TARGET
{
    float3 color = saturate( tex.Sample( texSampler, i.UV ).rgb );

    float2 p      = i.Position.xy;
    float3 dither = ( Hash( p ) + Hash( p + 17.0f ) - 1.0f ) / 255.0f;
    float3 ldr    = saturate( color + dither );

    // Rec.709 luma of the gamma-space color (what FXAA expects in alpha)
    float luma = dot( ldr, float3( 0.299f, 0.587f, 0.114f ) );

    return float4( ldr, luma );
}
