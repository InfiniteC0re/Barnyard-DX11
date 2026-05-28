#ifndef SHADER_UTILS_HLSLI
#define SHADER_UTILS_HLSLI

// Derives a world-space normal from the albedo texture's luminance gradient.
// Reconstructs TBN entirely from screen-space derivatives -- no tangent vertex data required.
// Texel step uses full derivative magnitude per UV axis so the result is correct
// regardless of how the UV is oriented relative to screen axes (walls, ceilings, etc.).
float3 ComputeDerivedWorldNormal(float3 worldPos, float3 worldNormal, float2 uv, Texture2D tex, SamplerState samp, float bumpScale)
{
	float3 N       = normalize(worldNormal);
	float3 dp1     = ddx(worldPos), dp2 = ddy(worldPos);
	float2 du1     = ddx(uv),       du2 = ddy(uv);
	float3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
	float3 T       = dp2perp * du1.x + dp1perp * du2.x;
	float3 B       = dp2perp * du1.y + dp1perp * du2.y;
	float  invmax  = rsqrt(max(dot(T, T), dot(B, B)));
	float3x3 TBN   = float3x3(T * invmax, B * invmax, N);

	float  tsU = length(float2(ddx(uv.x), ddy(uv.x)));
	float  tsV = length(float2(ddx(uv.y), ddy(uv.y)));
	float3 lum = float3(0.299f, 0.587f, 0.114f);
	float hL = dot(tex.Sample(samp, uv - float2(tsU, 0)).rgb, lum);
	float hR = dot(tex.Sample(samp, uv + float2(tsU, 0)).rgb, lum);
	float hD = dot(tex.Sample(samp, uv - float2(0, tsV)).rgb, lum);
	float hU = dot(tex.Sample(samp, uv + float2(0, tsV)).rgb, lum);

	float3 tangentNormal = normalize(float3((hL - hR) * bumpScale, (hU - hD) * bumpScale, 1.0f));
	return normalize(mul(tangentNormal, TBN));
}

#endif // SHADER_UTILS_HLSLI
