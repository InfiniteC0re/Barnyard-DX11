// STATIC: "NO_CSM" "0..1"
// STATIC: "NO_FOG" "0..1"
// STATIC: "NO_DYN_LIGHT" "0..1"

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
    float2 UV0 : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
    float ViewDepth : TEXCOORD2;
    float3 WorldNormal : TEXCOORD3;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matMVP;
	float4   cb_DisplaceOffset;
    float4   cb_AmbientColor;
	float4   cb_ShadowColor;
    float    cb_FogStart;
	float    cb_FogEnd;
	float4   cb_FogColor;
	float4   cb_WorldOffset;
    float4x4 cb_matModel;
};

#if !NO_CSM
#include "ShadowSampling.hlsli"
#endif

#if !NO_DYN_LIGHT
#include "DynamicLights.hlsli"
#endif

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

	// Calculate vertex screen position
    float3 objPos = In.ObjPos + (In.Normal + cb_DisplaceOffset.xyz) * cb_WorldOffset.xyz;
    Out.ProjPos = mul(float4(objPos, 1.0f), cb_matMVP);
    Out.WorldPos = mul(float4(objPos, 1.0f), cb_matModel).xyz;
    Out.ViewDepth = Out.ProjPos.w;
    Out.WorldNormal = normalize(mul(In.Normal, (float3x3)cb_matModel));

    Out.Color.xyz = lerp(cb_ShadowColor.xyz, cb_AmbientColor.xyz, In.Color.xyz);
    Out.Color.w = 1.0f;

    Out.UV0 = In.UV * cb_DisplaceOffset.w;

    return Out;
}

float CalculateExponentialFog(float distance, float fogStart, float density)
{
    if (distance <= fogStart) return 1.0f;
    return exp(-density * distance);
}

float CalculateExponentialSquaredFog(float distance, float fogStart, float density)
{
    if (distance <= fogStart) return 1.0f;
    return exp(-pow(density * (distance - fogStart), 2));
}

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

struct PS_OUT
{
    float4 Color   : SV_Target0;
    float4 GBuffer : SV_Target1;
};

PS_OUT ps_main(PS_IN In)
{
    float4 texColor = texture0.Sample(sampler0, In.UV0) * In.Color;
    if (texColor.a < 0.5f) discard;

#if !NO_DYN_LIGHT
	float3 glow = SampleDynamicGlowLights(In.WorldPos, In.WorldNormal);
	texColor.rgb = ApplyDynamicGlowLighting(texColor.rgb, glow);
#endif

#if !NO_CSM
    float shadow = SampleShadow(In.WorldPos, In.ViewDepth);
    float shadowStrength = cb_ShadowParams.w;
    float shadowScale = shadow * shadowStrength + (1.0f - shadowStrength);
    #if !NO_DYN_LIGHT
    shadowScale = lerp(shadowScale, 1.0f, saturate(max(glow.r, max(glow.g, glow.b))));
    #endif
#else
    float shadowScale = 1.0f;
#endif

#if !NO_FOG
    float fogFactor = CalculateExponentialSquaredFog(In.ProjPos.w, cb_FogStart, cb_FogColor.w);
	fogFactor = saturate(fogFactor);
    // Apply fog by blending between fog color and original color
    float3 finalColor = lerp(cb_FogColor.xyz, texColor.xyz * shadowScale, fogFactor);
#else
    float3 finalColor = texColor.xyz * shadowScale;
#endif

    PS_OUT Out;
    Out.Color   = float4(finalColor, texColor.a);
    Out.GBuffer = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return Out;
}
