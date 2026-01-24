struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD0;
};

struct PS_IN
{
    // float4 normal : NORMAL;
    // float4 color : COLOR;
    // float2 texcoord : TEXCOORD0;
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matMVP;
	float4 cb_TexCoordOffsetAndAlpha;
	float4 cb_AmbientColor;
	float4 cb_ShadowColor;
};

PS_IN vs_main(VS_IN input)
{
    PS_IN output;

    output.position = mul(float4(input.position, 1.0f), cb_matMVP);
    output.color = lerp(cb_ShadowColor, cb_AmbientColor, input.color);
    output.texcoord = input.texcoord + cb_TexCoordOffsetAndAlpha.xy;

    return output;
}

Texture2D albedo_texture : register(t0);
SamplerState albedo_texture_sampler : register(s0);

float4 ps_main(PS_IN input) : SV_TARGET
{
    float4 tex_color = albedo_texture.Sample(albedo_texture_sampler, input.texcoord);
    
	tex_color.a *= cb_TexCoordOffsetAndAlpha.z;
	
#ifdef ALPHAREF
    if (tex_color.a < 0.5f) discard;
#endif

    return tex_color * input.color;
}
