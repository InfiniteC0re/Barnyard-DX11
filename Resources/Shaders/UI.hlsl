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
#if defined(TEXTURED)

    float4 tex_color = ui_texture.Sample(ui_texture_sampler, input.texcoord);

    // tex_color.a = 0.3f;
    if (tex_color.a < 0.235f) discard;

    return tex_color * input.color;

#elif defined(FONT) // TEXTURED

    float sdf = ui_texture.Sample(ui_texture_sampler, input.texcoord).r;
    float pxRange = 0.03f;
    float outlineWidth = 0.1f;

    float alpha = smoothstep(0.5 - pxRange, 0.5 + pxRange, sdf);
    float outlineAlpha = smoothstep(0.5 - outlineWidth - pxRange, 0.5 - outlineWidth + pxRange, sdf);

    float4 finalColor = lerp(float4(0, 0, 0, input.color.a), input.color, alpha);
    finalColor.a *= max(alpha, outlineAlpha);

    return finalColor;

#else // FONT

    return input.color;

#endif // !TEXTURED && !FONT
}
