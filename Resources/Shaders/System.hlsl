// STATIC: "TEXTURED" "0..1"

#include "ShaderUtils.hlsli"

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

struct PS_OUT
{
    float4 Color   : SV_Target0;
    float4 GBuffer : SV_Target1; // system/2D geometry: no normal, not reflective
};

PS_OUT ps_main(PS_IN In)
{
#if TEXTURED
	float4 color = texture0.Sample(sampler0, In.UV0);

	color.xyz *= In.Color.xyz;
	color.w *= In.Color.w;
#else // TEXTURED
	float4 color = In.Color;
#endif // !TEXTURED

	// Dither into the R11G11B10 scene buffer so the untextured sky dome's gradient doesn't band on
	// the coarse 6/6/5 mantissa. See DitherR11G11B10 in ShaderUtils.hlsli
	color.xyz = DitherR11G11B10( color.xyz, In.ProjPos.xy );

    PS_OUT Out;
    Out.Color   = color;
    Out.GBuffer = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return Out;
}
