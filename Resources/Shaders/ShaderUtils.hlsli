#ifndef SHADER_UTILS_HLSLI
#define SHADER_UTILS_HLSLI

// Albedo-luminance micro-bump using the vertex tangent frame (world and skin both
// carry a generated tangent stream)
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

// Roughness-aware Fresnel for env specular (Lagarde approx, no BRDF LUT). Grazing angles brighten,
// floored by F0 at normal incidence
float3 F_SchlickRoughness(float NdotV, float3 F0, float roughness)
{
	float3 Fr = max((1.0f - roughness).xxx, F0);
	return F0 + (Fr - F0) * pow(saturate(1.0f - NdotV), 5.0f);
}

// Box parallax correction for a cube captured at a probe centre (relPos = worldPos - probeCentre).
// Returns box-centre -> ray exit on the box, so reflections stick to the proxy. Shared by SSR + IBL
float3 ParallaxCorrectReflection(float3 relPos, float3 reflDir, float3 halfExtents)
{
	float3 invDir = 1.0f / reflDir;
	float3 t1     = (-halfExtents - relPos) * invDir;
	float3 t2     = ( halfExtents - relPos) * invDir;
	float3 tMax   = max(t1, t2);
	float  t      = min(min(tMax.x, tMax.y), tMax.z);
	return relPos + reflDir * t;
}

// Cubemap env specular (IBL): samples the parallax-corrected reflection at a roughness-selected mip,
// weighted by roughness-aware Fresnel. Returns env * Fresnel; caller applies intensity + mask.
// relPos = worldPos - probeCentre
float3 EnvSpecular(TextureCube cube, SamplerState samp, float3 worldN, float3 V, float3 relPos,
                   float roughness, float maxMip, float3 F0, float3 boxHalfExtents)
{
	float3 N = worldN;
	if (dot(N, V) < 0.0f) N = -N;
	float  r     = saturate(roughness);
	float  NdotV = saturate(dot(N, V));
	float3 R     = reflect(-V, N);
	float3 dir   = ParallaxCorrectReflection(relPos, R, boxHalfExtents);
	float3 env   = cube.SampleLevel(samp, dir, r * maxMip).rgb;
	return env * F_SchlickRoughness(NdotV, F0, r);
}

// Triangular-PDF dither sized to the R11G11B10_FLOAT step (6/6/5 mantissa -> step = 2^(exp - mant)),
// applied at the store so low-contrast gradients don't band. Dithering after quantisation, or
// before a blur that averages the noise back out, does nothing
float DitherHash( float2 p )
{
	return frac( 52.9829189f * frac( dot( p, float2( 0.06711056f, 0.00583715f ) ) ) );
}

float3 DitherR11G11B10( float3 color, float2 pixel )
{
	float3 qStep = exp2( floor( log2( max( color, 1e-6f ) ) ) - float3( 6.0f, 6.0f, 5.0f ) );
	float  tri   = DitherHash( pixel ) + DitherHash( pixel + 17.0f ) - 1.0f; // triangular PDF in [-1,1]
	return color + tri * qStep;
}

#endif // SHADER_UTILS_HLSLI
