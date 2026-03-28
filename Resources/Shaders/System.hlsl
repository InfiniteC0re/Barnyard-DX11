struct VS_IN
{
    float3 ObjPos : POSITION;
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
};

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

	// Calculate vertex screen position
    Out.ProjPos = mul(float4(In.ObjPos, 1.0f), cb_matMVP);

    Out.Color = In.Color;

    Out.UV0 = In.UV;

    return Out;
}

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 ps_main(PS_IN In) : SV_TARGET
{
#ifdef TEXTURED
	float4 color = texture0.Sample(sampler0, In.UV0);

	color.xyz *= In.Color.xyz;
	color.w *= In.Color.w;
#else // TEXTURED
	float4 color = In.Color;
#endif // !TEXTURED
    
    return color;
}
