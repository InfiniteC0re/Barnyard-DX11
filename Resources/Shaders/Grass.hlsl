struct VS_IN
{
    float3 ObjPos : POSITION;
    float3 Normal : NORMAL;
    float4 Color : Color;
    float2 UV : TEXCOORD0;
};

struct PS_IN
{
    float4 ProjPos : SV_POSITION;
    float4 Color : Color;
    float2 UV0 : TEXCOORD;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matMVP;
	float4   cb_DisplaceOffset;
    float4   cb_AmbientColor;
	float4   cb_ShadowColor;
	float4   cb_WorldOffset;
};

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

	// Calculate vertex screen position
    Out.ProjPos = mul(float4(In.ObjPos + (In.Normal + cb_DisplaceOffset.xyz) * cb_WorldOffset.xyz, 1.0f), cb_matMVP);

    Out.Color.xyz = lerp(cb_ShadowColor.xyz, cb_AmbientColor.xyz, In.Color.xyz);
    Out.Color.w = 1.0f;

    Out.UV0 = In.UV * cb_DisplaceOffset.w;

    return Out;
}

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 ps_main(PS_IN In) : SV_TARGET
{
    float4 tex_color = texture0.Sample(sampler0, In.UV0) * In.Color;
    if (tex_color.a < 0.5f) discard;

    return tex_color;
}
