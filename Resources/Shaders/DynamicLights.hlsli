cbuffer DynamicGlowLights : register(b2)
{
	float4   cb_glowLightPositionRadius[4]; // xyz = world position, w = radius
	float4   cb_glowLightDirectionCone[4];  // xyz = direction (light toward scene), w = cosOuter
	float4x4 cb_glowLightVP[4];
	float4   cb_glowLightShadowParams[4];   // x = shadow slice, y = texel size, z = bias, w = strength
	float4   cb_glowLightColor[4];          // xyz = light colour, w = volumetric intensity
	float4   cb_glowLightIntensity[4];      // x = surface intensity, y = bump scale, z = cosInner
	float4   cb_glowLightParams;            // x = count
};

Texture2DArray         dynamicGlowShadowMaps    : register(t6);
SamplerComparisonState dynamicGlowShadowSampler : register(s6);

float4 GetDynamicGlowProjectedPosition(float3 worldPos, int lightIndex)
{
	float4 shadowPos = mul(float4(worldPos, 1.0f), cb_glowLightVP[lightIndex]);
	shadowPos.xyz /= shadowPos.w;
	return shadowPos;
}

float SampleDynamicGlowShadow(float3 worldPos, int lightIndex, float4 shadowPos)
{
	float4 shadowParams = cb_glowLightShadowParams[lightIndex];
	if (shadowParams.x < 0.0f || shadowParams.w <= 0.0f)
		return 1.0f;

	float2 shadowUV = shadowPos.xy * float2(0.5f, -0.5f) + 0.5f;
	float shadowZ = shadowPos.z - shadowParams.z;

	if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f) || shadowPos.z < 0.0f || shadowPos.z > 1.0f)
		return 1.0f;

	float texelSize = shadowParams.y;
	float slice = shadowParams.x;
	float shadow = 0.0f;

	[unroll] for (int y = -1; y <= 1; y++)
	{
		[unroll] for (int x = -1; x <= 1; x++)
		{
			shadow += dynamicGlowShadowMaps.SampleCmpLevelZero(
				dynamicGlowShadowSampler,
				float3(shadowUV + float2(x, y) * texelSize, slice),
				shadowZ
			);
		}
	}

	shadow *= 1.0f / 9.0f;
	return lerp(1.0f, shadow, shadowParams.w);
}

#include "ShaderUtils.hlsli"

// Returns the diffuse contribution of one light; accumulates its Blinn-Phong specular
// into a_specular (skipped when specInt <= 0 or the spec normal is degenerate). `normal`
// drives the diffuse NdotL; `specNormal` drives the specular highlight (kept separate so
// the specular can use a clean normal while the diffuse uses a bumpier one).
float3 SampleDynamicGlowLight(float3 worldPos, float3 normal, float3 specNormal, float3 viewDir, int lightIndex, float intensity, float specInt, float specPow, inout float3 a_specular)
{
	float4 lightPositionRadius = cb_glowLightPositionRadius[lightIndex];
	float4 lightDirectionCone  = cb_glowLightDirectionCone[lightIndex];
	float4 lightProjPos        = GetDynamicGlowProjectedPosition(worldPos, lightIndex);
	float2 lightUV             = lightProjPos.xy * float2(0.5f, -0.5f) + 0.5f;

	if (any(lightUV < 0.0f) || any(lightUV > 1.0f) || lightProjPos.z < 0.0f || lightProjPos.z > 1.0f)
		return 0.0f;

	float3 lightToPixel = worldPos - lightPositionRadius.xyz;
	float  dist         = length(lightToPixel);
	float  radius       = max(lightPositionRadius.w, 0.001f);

	float d = saturate(dist / radius);
	float smoothCutoff = saturate(1.0f - d * d);
	smoothCutoff *= smoothCutoff;
	float distAttenuation = smoothCutoff * (1.75f / (1.0f + 8.0f * d * d));

	float3 lightDir        = lightToPixel / max(dist, 0.001f);
	float  NdotL           = (dot(normal, normal) > 0.25f) ? saturate(dot(normal, -lightDir)) : 1.0f;
	float  cosAngle        = dot(lightDir, lightDirectionCone.xyz);
	float  cosOuter        = lightDirectionCone.w;
	float  cosInner        = cb_glowLightIntensity[lightIndex].z;
	float  spotAttenuation = smoothstep(cosOuter, cosInner, cosAngle);
	float2 edgeFade        = saturate(min(lightUV, 1.0f - lightUV) * 8.0f);
	float  projectionFade  = edgeFade.x * edgeFade.y;
	float  shadow          = SampleDynamicGlowShadow(worldPos, lightIndex, lightProjPos);

	// Attenuation shared by diffuse and specular (everything but NdotL and the material term).
	float  attenuation = distAttenuation * spotAttenuation * projectionFade * shadow * intensity;

	if (specInt > 0.0f && dot(specNormal, specNormal) > 0.25f && NdotL > 0.0f)
	{
		float3 H        = normalize(-lightDir + viewDir);   // half vector (L = -lightDir = toward light)
		float  specTerm = pow(saturate(dot(specNormal, H)), specPow);
		a_specular += cb_glowLightColor[lightIndex].rgb * (specTerm * specInt * attenuation);
	}

	return cb_glowLightColor[lightIndex].rgb * (attenuation * NdotL);
}

// Surface lighting with specular -- returns diffuse glow, accumulates specular into
// a_specular. `normal` drives diffuse, `specNormal` drives the highlight (see per-light).
float3 SampleDynamicGlowLights(float3 worldPos, float3 normal, float3 specNormal, float3 viewDir, float specInt, float specPow, out float3 a_specular)
{
	float3 glow       = 0.0f;
	a_specular        = 0.0f;
	int    lightCount = (int)cb_glowLightParams.x;

	if (lightCount > 0) glow += SampleDynamicGlowLight(worldPos, normal, specNormal, viewDir, 0, cb_glowLightIntensity[0].x, specInt, specPow, a_specular);
	if (lightCount > 1) glow += SampleDynamicGlowLight(worldPos, normal, specNormal, viewDir, 1, cb_glowLightIntensity[1].x, specInt, specPow, a_specular);
	if (lightCount > 2) glow += SampleDynamicGlowLight(worldPos, normal, specNormal, viewDir, 2, cb_glowLightIntensity[2].x, specInt, specPow, a_specular);
	if (lightCount > 3) glow += SampleDynamicGlowLight(worldPos, normal, specNormal, viewDir, 3, cb_glowLightIntensity[3].x, specInt, specPow, a_specular);

	return glow;
}

// Diffuse-only surface lighting (no specular) -- used by the skin shader.
float3 SampleDynamicGlowLights(float3 worldPos, float3 normal)
{
	float3 dummySpecular;
	return SampleDynamicGlowLights(worldPos, normal, normal, float3(0, 0, 0), 0.0f, 1.0f, dummySpecular);
}

// Volumetric fog sampling -- uses per-light volumetric intensity, no surface normal (NdotL = 1)
float3 SampleDynamicGlowLights(float3 worldPos)
{
	float3 glow       = 0.0f;
	float3 dummySpec  = 0.0f;
	int    lightCount = (int)cb_glowLightParams.x;

	if (lightCount > 0) glow += SampleDynamicGlowLight(worldPos, float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0), 0, cb_glowLightColor[0].w, 0.0f, 1.0f, dummySpec);
	if (lightCount > 1) glow += SampleDynamicGlowLight(worldPos, float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0), 1, cb_glowLightColor[1].w, 0.0f, 1.0f, dummySpec);
	if (lightCount > 2) glow += SampleDynamicGlowLight(worldPos, float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0), 2, cb_glowLightColor[2].w, 0.0f, 1.0f, dummySpec);
	if (lightCount > 3) glow += SampleDynamicGlowLight(worldPos, float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0), 3, cb_glowLightColor[3].w, 0.0f, 1.0f, dummySpec);

	return glow;
}

float3 ApplyDynamicGlowLighting(float3 baseColor, float3 glow)
{
	// Scale the surface by the light contribution.
	// Preserves the surface hue: white light on red grass stays red, just brighter.
	// Channels are multiplied proportionally so color ratios are maintained.
	return baseColor * (1.0f + glow);
}
