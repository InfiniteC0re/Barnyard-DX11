float3 SampleSimplePointLight(float3 worldPos, float3 normal, float3 lightColor, float4 lightPositionIntensity)
{
	float3 toLight = lightPositionIntensity.xyz - worldPos;
	float  distSq  = max(dot(toLight, toLight), 0.0001f);
	float3 L       = toLight * rsqrt(distSq);
	float  NdotL   = saturate(dot(normal, L));
	float  atten   = lightPositionIntensity.w / (1.0f + distSq);

	return lightColor * (NdotL * atten);
}

float3 SampleSimplePointLights(float3 worldPos, float3 normal)
{
	float3 result     = 0.0f;
	int    lightCount = (int)cb_simplePointLightParams.x;

	if (lightCount > 0) result += SampleSimplePointLight(worldPos, normal, cb_simplePointLightColor[0].rgb, cb_simplePointLightPositionIntensity[0]);
	if (lightCount > 1) result += SampleSimplePointLight(worldPos, normal, cb_simplePointLightColor[1].rgb, cb_simplePointLightPositionIntensity[1]);
	if (lightCount > 2) result += SampleSimplePointLight(worldPos, normal, cb_simplePointLightColor[2].rgb, cb_simplePointLightPositionIntensity[2]);
	if (lightCount > 3) result += SampleSimplePointLight(worldPos, normal, cb_simplePointLightColor[3].rgb, cb_simplePointLightPositionIntensity[3]);

	return result;
}
