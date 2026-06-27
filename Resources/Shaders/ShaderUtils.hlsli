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

// Same albedo-luminance micro-bump as above, but using a supplied vertex tangent
// frame instead of reconstructing the TBN from screen-space derivatives. Cheaper
// (no worldPos gradient / perp crosses) and cleaner at UV seams. For meshes that
// carry a tangent stream (world geometry); skinned meshes use the derivative path.
float3 ComputeDerivedWorldNormalT(float3 worldNormal, float3 tangent, float handedness, float2 uv, Texture2D tex, SamplerState samp, float bumpScale)
{
	float3 N = normalize(worldNormal);
	float3 T = normalize(tangent - N * dot(N, tangent)); // Gram-Schmidt against the (possibly normal-mapped) N
	float3 B = cross(N, T) * handedness;
	float3x3 TBN = float3x3(T, B, N);

	// uv derivatives are still needed for the texel step (mip-correct sample distance).
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

// Transform a tangent-space normal into world space using a TBN reconstructed from screen
// derivatives (no vertex tangents needed). Used by the skin shader, which is skinned/animated
// and so can't carry a precomputed tangent stream the way the world meshes do.
float3 PerturbNormalDeriv(float3 worldPos, float3 N, float2 uv, float3 tangentNormal)
{
	float3 dp1     = ddx(worldPos), dp2 = ddy(worldPos);
	float2 du1     = ddx(uv),       du2 = ddy(uv);
	float3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
	float3 T       = dp2perp * du1.x + dp1perp * du2.x;
	float3 B       = dp2perp * du1.y + dp1perp * du2.y;
	float  invmax  = rsqrt(max(dot(T, T), dot(B, B)));
	return normalize(tangentNormal.x * T * invmax + tangentNormal.y * B * invmax + tangentNormal.z * N);
}

#endif // SHADER_UTILS_HLSLI
