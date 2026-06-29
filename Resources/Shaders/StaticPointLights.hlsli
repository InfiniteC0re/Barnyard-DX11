// Static point lights: fixed, never-moving lights loaded from the level. Their data lives
// in one global cbuffer (uploaded once per frame); each draw passes the up-to-4 indices of
// the lights touching its cell, which we look up here.
//
// STATIC_POINT_LIGHT_COUNT must match remaster::MAX_STATIC_POINT_LIGHTS in LightManager.h.
#define STATIC_POINT_LIGHT_COUNT 64

cbuffer StaticPointLights : register(b3)
{
	float4 cb_staticLightPositionRadius[STATIC_POINT_LIGHT_COUNT]; // xyz = world pos, w = radius
	float4 cb_staticLightColorIntensity[STATIC_POINT_LIGHT_COUNT]; // xyz = RGB,       w = intensity
};

float3 SampleStaticPointLight(float3 worldPos, float3 normal, int index)
{
	float4 posRadius = cb_staticLightPositionRadius[index];
	float4 colInt    = cb_staticLightColorIntensity[index];

	float3 toLight = posRadius.xyz - worldPos;
	float  dist    = length(toLight);
	float  radius  = max(posRadius.w, 0.001f);

	// Smooth falloff that reaches exactly 0 at the radius (no hard clip).
	float d     = saturate(dist / radius);
	float atten = saturate(1.0f - d * d);
	atten      *= atten;

	float3 L     = toLight / max(dist, 0.001f);
	// Unlit/degenerate normals (e.g. fog) get full wrap; otherwise standard N.L.
	float  NdotL = (dot(normal, normal) > 0.25f) ? saturate(dot(normal, L)) : 1.0f;

	return colInt.rgb * (colInt.w * atten * NdotL);
}

// indices.xyzw = up to `count` indices into the global static-light arrays (per-cell).
float3 SampleStaticPointLights(float3 worldPos, float3 normal, float4 indices, int count)
{
	float3 result = 0.0f;
	if (count > 0) result += SampleStaticPointLight(worldPos, normal, (int)indices.x);
	if (count > 1) result += SampleStaticPointLight(worldPos, normal, (int)indices.y);
	if (count > 2) result += SampleStaticPointLight(worldPos, normal, (int)indices.z);
	if (count > 3) result += SampleStaticPointLight(worldPos, normal, (int)indices.w);
	return result;
}
