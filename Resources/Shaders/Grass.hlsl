// STATIC: "NO_CSM" "0..1" [ps]
// STATIC: "NO_FOG" "0..1" [ps]
// STATIC: "NO_DYN_LIGHT" "0..1" [ps]
// STATIC: "CLOUD_SHADOWS" "0..1" [ps]

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
    centroid float4 Color : Color;
    centroid float2 UV0 : TEXCOORD0;
    centroid float3 WorldPos : TEXCOORD1;
    centroid float3 WorldNormal : TEXCOORD3;
};

// Per-draw constants. Per-pass values (ambient/shadow colours, fog) live in PerPass.hlsli (b4)
cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matModel;                // 0-3 (clip position = world * pp_matViewProj)
    float4   cb_DisplaceOffset;          // 4
    float4   cb_WorldOffset;             // 5
    float4   cb_cellStaticLightIndices;  // 6: xyzw = up to 4 static light indices into the global buffer (-1 = none)
    float4   cb_cellStaticLightParams;   // 7: x = count
    float4   cb_cellStaticLightIndices2; // 8: xyzw = static light indices 4..7 (-1 = none)
};

#include "PerPass.hlsli"

#if !NO_CSM
#include "ShadowSampling.hlsli"
#endif

#if !NO_DYN_LIGHT
#include "DynamicLights.hlsli"
#endif

#include "StaticPointLights.hlsli"

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

    float3 objPos = In.ObjPos + (In.Normal + cb_DisplaceOffset.xyz) * cb_WorldOffset.xyz;
    Out.WorldPos = mul(float4(objPos, 1.0f), cb_matModel).xyz;
    Out.ProjPos = mul(float4(Out.WorldPos, 1.0f), pp_matViewProj);
    Out.WorldNormal = normalize(mul(In.Normal, (float3x3)cb_matModel));

    Out.Color.xyz = lerp(pp_ShadowColor.xyz, pp_AmbientColor.xyz, In.Color.xyz);
    Out.Color.w = 1.0f;

    Out.UV0 = In.UV * cb_DisplaceOffset.w;

    return Out;
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

#define GRASS_LIGHT_SATURATION_GAIN 1.2f

PS_OUT ps_main(PS_IN In)
{
    float4 texRaw   = texture0.Sample(sampler0, In.UV0);
    float4 texColor = texRaw * In.Color;
    if (texColor.a < 0.5f) discard;

	// Point lights (static + dynamic glow) applied additively on the raw texture below, after the
	// sun shadow: extra incoming light neither scales with the baked shading nor dims in shadow.
	// The foliage variant adds wrap diffuse + translucency so backlit blades glow through
	float3 V = normalize(pp_CameraPos.xyz - In.WorldPos);
	float3 pointLight = SampleStaticPointLightsFoliage(In.WorldPos, normalize(In.WorldNormal), V, cb_cellStaticLightIndices, cb_cellStaticLightIndices2, (int)cb_cellStaticLightParams.x);
#if !NO_DYN_LIGHT
	pointLight += SampleDynamicGlowLights(In.WorldPos, In.WorldNormal);
#endif

#if !NO_CSM
    float shadow = SampleShadow(In.WorldPos, In.WorldNormal, In.ProjPos.w);
    float shadowStrength = cb_ShadowParams.w;
    // No glow-based un-shadowing lerp anymore: additive point light is not scaled by the sun
    // shadow, so lit grass in shade brightens on its own
    float shadowScale = shadow * shadowStrength + (1.0f - shadowStrength);
#else
    float shadowScale = 1.0f;
#endif

    // Additive point-light contribution, with the saturation pop ApplyGrassLight used to give
    // glow lights so lit grass keeps its colour instead of washing toward the light tint
    float3 lightAdd = texRaw.rgb * pointLight;
    float  addAmt   = saturate(dot(pointLight, float3(0.333f, 0.333f, 0.333f)));
    float  addLum   = dot(lightAdd, float3(0.299f, 0.587f, 0.114f));
    lightAdd        = max(0.0f, addLum + (lightAdd - addLum) * (1.0f + addAmt * GRASS_LIGHT_SATURATION_GAIN));

    float3 litColor = texColor.xyz * shadowScale + lightAdd;

#if !NO_FOG
    float fogFactor = CalculateExponentialSquaredFog(In.ProjPos.w, pp_FogParams.x, pp_FogColor.w);
	fogFactor = saturate(fogFactor);
    // Apply fog by blending between fog color and original color
    float3 finalColor = lerp(pp_FogColor.xyz, litColor, fogFactor);
#else
    float3 finalColor = litColor;
#endif

    PS_OUT Out;
    Out.Color   = float4(finalColor, texColor.a);
    Out.GBuffer = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return Out;
}
