// STATIC: "ALPHAREF" "0..1"
// STATIC: "NO_CSM" "0..1"
// STATIC: "NO_FOG" "0..1"
// STATIC: "NO_DYN_LIGHT" "0..1"
// STATIC: "GLOW" "0..1"

struct VS_IN
{
    float3 ObjPos : POSITION;
    float3 normal : NORMAL;
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
	float4   cb_TexCoordOffsetAndAlpha;
	float4   cb_AmbientColor;
	float4   cb_ShadowColor;
	float    cb_IsWater;
	float    cb_IsLit;
	float    cb_FogStart;
	float    cb_FogEnd;
	float4   cb_FogColor;
    float4x4 cb_matModel;
};

#if !NO_CSM
#include "ShadowSampling.hlsli"
#endif

#if !NO_DYN_LIGHT
#include "DynamicLights.hlsli"
#endif

#include "Tonemap.hlsli"

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

	// Calculate vertex screen position
    Out.ProjPos = mul(float4(In.ObjPos, 1.0f), cb_matMVP);
    Out.WorldPos = mul(float4(In.ObjPos, 1.0f), cb_matModel).xyz;
    Out.ViewDepth = Out.ProjPos.w;
    Out.WorldNormal = normalize(mul(In.normal, (float3x3)cb_matModel));
    
	// Calculate vertex Color based on current lighting settings and shadow factor (stored as vertex data)
	Out.Color.xyz = lerp(cb_ShadowColor.xyz, cb_AmbientColor.xyz, In.Color.xyz);
    
	// Adjust water Color
	// Water recieves less ambient Color
	Out.Color.xyz = (In.Color.xyz * cb_IsWater * 0.75f + Out.Color.xyz * 0.25f * cb_IsWater) + (Out.Color.xyz * cb_IsLit);

	// Calculate opacity
	// Used for water rings and probably something else
	Out.Color.w = (In.Color.x * cb_IsWater) + (1.0f * cb_IsLit);
	
	// Animate UV
    Out.UV0 = In.UV + cb_TexCoordOffsetAndAlpha.xy;

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

float4 ps_main(PS_IN In) : SV_TARGET
{
    float4 texColor = texture0.Sample(sampler0, In.UV0) * In.Color * cb_TexCoordOffsetAndAlpha.z;
	
#if ALPHAREF
	// The only alpharef value used by the game is 128 (0.5f)
    if (texColor.a < 0.5f) discard;
#endif

#if !NO_DYN_LIGHT
	float3 glow = SampleDynamicGlowLights(In.WorldPos, ComputeDerivedWorldNormal(In.WorldPos, In.WorldNormal, In.UV0, texture0, sampler0, cb_glowLightIntensity[0].y));
	texColor.rgb = ApplyDynamicGlowLighting(texColor.rgb, glow);
#endif

#if !NO_CSM
    float shadow = SampleShadow(In.WorldPos, In.ViewDepth);
    float shadowStrength = cb_ShadowParams.w;
    float shadowScale = shadow * shadowStrength + (1.0f - shadowStrength);
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

#if GLOW
    finalColor *= float3(255, 252, 204) / 255.0f;
#endif
    
    return float4(finalColor, texColor.a);
}
