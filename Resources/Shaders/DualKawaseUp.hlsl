#include "ScreenSpace.hlsl"

Texture2D    tex     : register( t0 );
SamplerState texSamp : register( s0 );

cbuffer KawaseCBuffer : register( b1 )
{
    float2 cb_texelSize;
    float  cb_offset;
    float  cb_PADDING;
};

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float2 halfpixel = cb_texelSize * 0.5;
    float2 o         = halfpixel * cb_offset;

    float4 color = (float4)0.0;
    color += tex.Sample( texSamp, i.UV + float2( -o.x * 2.0,        0.0 ) );
    color += tex.Sample( texSamp, i.UV + float2(  o.x * 2.0,        0.0 ) );
    color += tex.Sample( texSamp, i.UV + float2(        0.0, -o.y * 2.0 ) );
    color += tex.Sample( texSamp, i.UV + float2(        0.0,  o.y * 2.0 ) );
    color += tex.Sample( texSamp, i.UV + float2( -o.x,  o.y ) ) * 2.0;
    color += tex.Sample( texSamp, i.UV + float2(  o.x,  o.y ) ) * 2.0;
    color += tex.Sample( texSamp, i.UV + float2( -o.x, -o.y ) ) * 2.0;
    color += tex.Sample( texSamp, i.UV + float2(  o.x, -o.y ) ) * 2.0;

    return color / 12.0;
}
