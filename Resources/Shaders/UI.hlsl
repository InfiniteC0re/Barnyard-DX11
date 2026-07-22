// STATIC: "TEXTURED" "0..1"
// STATIC: "FONT" "0..1"
// STATIC: "ALPHA_REF" "0..1"

struct VS_IN
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

struct PS_IN
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 ui_projection;
    float4x4 ui_view;
};

PS_IN vs_main(VS_IN input)
{
    PS_IN output;

    output.position = mul(mul(float4(input.position, 1.0f), ui_view), ui_projection);
    output.color = input.color;
    output.texcoord = input.texcoord;

    return output;
}

Texture2D ui_texture : register(t0);
SamplerState ui_texture_sampler : register(s0);

float4 ps_main(PS_IN input) : SV_TARGET
{
#if TEXTURED

    float4 texColor = ui_texture.SampleLevel(ui_texture_sampler, input.texcoord, 0);
	
#if ALPHA_REF
    clip(texColor.a - 60.0f / 255.0f);
#endif

    return texColor * input.color;

#elif FONT // TEXTURED

    float sdf = ui_texture.SampleLevel(ui_texture_sampler, input.texcoord, 0).r;
    float outlineWidth = 0.1f;

    // Screen-space anti-aliasing: 1/fwidth is how many distance-field units cover
    // one screen pixel, so the edge stays a ~1px ramp at any text scale
    float aaInv = 1.0f / max(fwidth(sdf), 1e-5f);

    float alpha = saturate((sdf - 0.5f) * aaInv + 0.5f);
    float outlineAlpha = saturate((sdf - (0.5f - outlineWidth)) * aaInv + 0.5f);

    float4 finalColor = lerp(float4(0, 0, 0, input.color.a), input.color, alpha);
    finalColor.a *= max(alpha, outlineAlpha);

    return finalColor;

#else // FONT

    return input.color;

#endif // !TEXTURED && !FONT
}
