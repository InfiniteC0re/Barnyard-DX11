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
	float2 cb_TexCoordOffset;
};

PS_IN vs_main(VS_IN input)
{
    PS_IN output;

    output.position = mul(float4(input.position, 1.0f), cb_matMVP);
    output.color = input.color;
    output.texcoord = input.texcoord + cb_TexCoordOffset;

    return output;
}

Texture2D albedo_texture : register(t0);
SamplerState albedo_texture_sampler : register(s0);

float4 ps_main(PS_IN input) : SV_TARGET
{
    float4 tex_color = albedo_texture.Sample(albedo_texture_sampler, input.texcoord);
    
#ifdef ALPHAREF
    if (tex_color.a < 0.5f) discard;
#endif

    return tex_color * input.color;
}
