// STATIC: "BAKED_LIGHTING" "0..1"
// STATIC: "FOB" "0..1"
// STATIC: "NO_CSM" "0..1"
// STATIC: "NO_FOG" "0..1"
// STATIC: "NO_DYN_LIGHT" "0..1"
// STATIC: "ANIMATED" "0..1"
// STATIC: "MATERIAL_MAPS" "0..1"
// STATIC: "CLOUD_SHADOWS" "0..1"

struct VS_IN
{
	float3 ObjPos	: POSITION;		// Object space position
    float3 Normal	: NORMAL;		// Vertex normal
	float4 Weights	: BLENDWEIGHT;	// Weights
	float4 MIndices	: BLENDINDICES;	// Matrix Indices
	float2 UV		: TEXCOORD0;	// UV
};

struct PS_IN
{
	float4 ProjPos	: SV_POSITION;		// Projected space position 
	// float3 Normal	: NORMAL;
	float2 UV0		: TEXCOORD0;		// UV
	// float4 ViewPos	: TEXCOORD3;	// View space position
	// float FogFactor	: FOG;
#if BAKED_LIGHTING
	float2 UV1		: TEXCOORD1;		// UV for baked lighting
	float4 LightingLerp1 : TEXCOORD5;
	float4 LightingLerp2 : TEXCOORD6;
#else // BAKED_LIGHTING
	float4 Color	: COLOR;        // xyz = ambient (FOB: full hardcoded colour), w = alpha
	float3 DirectLight : TEXCOORD8; // NdotL * lightColor -- the sun term, gated by shadow in the PS
#endif // !BAKED_LIGHTING
	float3 WorldPos    : TEXCOORD2;
	float ViewDepth    : TEXCOORD3;
	float AlphaRef     : TEXCOORD4;
	float3 WorldNormal : TEXCOORD7;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matWVP;
	float4	 cb_ambientColor;
	float4	 cb_lightColor;		// xyz = light color, w = alpha ref
	float4	 cb_lightDirection;
	float4	 cb_upAxis;
	float4	 cb_lightingLerp;
	float    cb_FogStart;
	float    cb_FogEnd;
	float    cb_SpecPower;      // 9.z: specular shininess exponent
	float    cb_SpecIntensity;  // 9.w: specular strength
	float4   cb_FogColor;
    float4x4 cb_matModel;
    float4   cb_CameraPos;      // 15: xyz = camera world position
    float4   cb_MapParams;      // 16: x = normal strength, y = roughness strength, z = roughness, w = map flags (1=normal,2=rough,3=both)
    float4   cb_SSRParams;      // 17: x = SSR reflectivity, y = fresnel power, z = emissive intensity (1 = neutral)
};

#ifdef ANIMATED

cbuffer BoneCBuffer : register(b1)
{
    float4x4 cb_bones[28];
};

#endif // ANIMATED

#define SHADOW_TEXTURE_REGISTER t5
#define SHADOW_SAMPLER_REGISTER s5

#if !NO_CSM
#include "ShadowSampling.hlsli"
#endif

#if !NO_DYN_LIGHT
#include "DynamicLights.hlsli"
#endif

#include "ShaderUtils.hlsli" // PerturbNormalDeriv (derivative TBN normal mapping)
#include "GBuffer.hlsli"     // OctEncodeNormal / PackFresnelRoughness for SSR

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

#if ANIMATED

	// Animate bones
    float BoneWeights[4];
	BoneWeights[0] = In.Weights.x;
	BoneWeights[1] = In.Weights.y;
	BoneWeights[2] = In.Weights.z;
	BoneWeights[3] = In.Weights.w;

    int BoneIndices[4];
	BoneIndices[0] = (int)(In.MIndices.x * 255.0 / 3);
	BoneIndices[1] = (int)(In.MIndices.y * 255.0 / 3);
	BoneIndices[2] = (int)(In.MIndices.z * 255.0 / 3);
	BoneIndices[3] = (int)(In.MIndices.w * 255.0 / 3);

    float3 vertex = 0;
	float3 normal = 0;
	for (int i = 0; i < 4; ++i)
	{
		float4x3 BoneMatrix = (float4x3)cb_bones[BoneIndices[i]];
		vertex += mul(float4(In.ObjPos, 1.0), BoneMatrix) * BoneWeights[i];

		float3x3 BoneNormal = (float3x3)BoneMatrix;
		normal += mul(In.Normal, BoneNormal) * BoneWeights[i];
	}
	
#else // ANIMATED
	
	float3 vertex = In.ObjPos;
	float3 normal = In.Normal;

#endif // !ANIMATED

	Out.ProjPos = mul(float4(vertex, 1.0), cb_matWVP);
	Out.ViewDepth = Out.ProjPos.w;
	Out.AlphaRef = cb_lightColor.w;

	// cb_matWVP = ModelView * Proj; strip Proj to get view-space, then apply ViewWorld to get world-space.
	// For skinned meshes, bones may already be world-space -- use cb_matModel only if it's truly model->world.
	// If shadows still drift with camera position, switch to: Out.WorldPos = vertex;
	Out.WorldPos = mul(float4(vertex, 1.0), cb_matModel).xyz;
	Out.WorldNormal = normalize(mul(normal, (float3x3)cb_matModel));
    Out.UV0 = In.UV;

	// cb_lightDirection is pre-multiplied by the inverse model on the CPU. Bring it back to
	// world space here so rotating models don't lock the sun to a body-relative direction.
	float3 worldLightDirVS = mul(cb_lightDirection.xyz, (float3x3)cb_matModel);
	float  NdotL           = dot(Out.WorldNormal, -worldLightDirVS);

#if BAKED_LIGHTING

	// Baked Characters Lighting
	NdotL = clamp(NdotL, 0.0f, 1.0f);

    Out.UV1.x = NdotL;
    Out.UV1.y = dot(normal, cb_upAxis.xyz);
	Out.LightingLerp1 = cb_lightingLerp;
	Out.LightingLerp2 = float4(1.0f, 1.0f, 1.0f, 0.0f) - cb_lightingLerp;

#elif FOB // BAKED_LIGHTING

	// FOB Lighting
	// NdotL = abs(NdotL);
	NdotL = clamp(NdotL, 0.0f, 1.0f);

	// float3 lightColor = cb_ambientColor.xyz;
	// lightColor.r *= 0.9f;
	// lightColor.z *= 0.5f;

	// lightColor += NdotL * 1.2f * cb_lightColor.xyz;

	// Out.Color.xyz = lightColor;

	const float3 baseColor = float3(0.54509807f, 0.60784316f, 0.47058824f);
	const float3 lightColor = float3(0.7372549f, 0.8156863f, 0.5254902f);
	// float3(0.54509807f, 0.60784316f, 0.47058824f) - usual
	// float3(0.9529412f, 0.75686276f, 0.54509807f) - yellow
	// float3(0.7372549f, 0.8156863f, 0.5254902f) - lighted

	float3 colorA = float3(188, 201, 103) / 255.0f;
	float3 colorB = float3(111, 114, 143) / 255.0f;

	// Out.Color.xyz = NdotL * cb_lightColor.xyz + cb_ambientColor.xyz;
	// Out.Color.xyz = float3(1.0f, 1.0f, 1.0f) * (NdotL * lightColor + (1.0f - NdotL) * baseColor);
	Out.Color.xyz = colorB + (colorA - colorB) * 1;
	Out.Color.w = cb_ambientColor.a;
	Out.DirectLight = float3(0.0f, 0.0f, 0.0f); // FOB is a flat hardcoded look, no directional term
	// Out.Color.xyz = lerp(float3(0.54509807f, 0.60784316f, 0.47058824f), cb_lightColor.xyz + float3(0.7372549f, 0.8156863f, 0.5254902f), NdotL);

#else // !FOB && !BAKED_LIGHTING

	// Runtime lighting calculation

	NdotL = clamp(NdotL, 0.0f, 1.0f);

	// Original PC shading:
	// Out.Color.xyz = NdotL * cb_lightColor.xyz + (1.0f - NdotL) * cb_ambientColor.xyz;

	// Correct console shading -- ambient stays, directional (sun) is split out so the PS can
	// gate it by the shadow factor (no sun term in shade), like a specular highlight.
	Out.Color.xyz   = cb_ambientColor.xyz;
	Out.DirectLight = NdotL * cb_lightColor.xyz;
	Out.Color.w     = cb_ambientColor.a;

#endif // !BAKED_LIGHTING

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
Texture2D normalMap    : register(t7); // per-material normal map (t1-t4 are baked lighting)
Texture2D roughnessMap : register(t8); // per-material roughness (R channel)

#if BAKED_LIGHTING
Texture2D lighting1 : register(t1);
Texture2D lighting2 : register(t2);
Texture2D lighting3 : register(t3);
Texture2D lighting4 : register(t4);
SamplerState samplerLighting : register(s1);
#endif // BAKED_LIGHTING

struct PS_OUT
{
    float4 Color   : SV_Target0;
    float4 GBuffer : SV_Target1; // rgb = world-space normal, a = reflectivity (0; skin is non-reflective)
};

PS_OUT ps_main(PS_IN In, bool a_bFrontFace : SV_IsFrontFace)
{
    float4 texColor = texture0.Sample(sampler0, In.UV0);
	clip(texColor.a - In.AlphaRef);

	// Sun shadow visibility, up front so the directional lighting term can be gated by it.
#if !NO_CSM
	float shadow = SampleShadow(In.WorldPos, In.WorldNormal, In.ViewDepth);
#else
	float shadow = 1.0f;
#endif

#if BAKED_LIGHTING
	float3 lighting1Color = lighting1.Sample(samplerLighting, In.UV1).rgb;
	float3 lighting2Color = lighting2.Sample(samplerLighting, In.UV1).rgb;
	float3 lighting3Color = lighting3.Sample(samplerLighting, In.UV1).rgb;
	float3 lighting4Color = lighting4.Sample(samplerLighting, In.UV1).rgb;

	texColor.rgb = clamp(texColor.rgb - lerp(lighting3Color, lighting1Color, In.LightingLerp1.xxx), 0.0f, 1.0f);
	texColor.rgb = clamp(texColor.rgb + lerp(lighting2Color, lighting4Color, In.LightingLerp2.xxx), 0.0f, 1.0f);
	texColor.a *= cb_ambientColor.a;
#else // BAKED_LIGHTING
	// Ambient (In.Color) always; the directional sun term is gated by shadow so it vanishes
	// in shade (FOB has DirectLight = 0, so it's unaffected).
	texColor.rgb = texColor.rgb * (In.Color.rgb + In.DirectLight * shadow);
	texColor.a *= cb_ambientColor.a;
#endif // !BAKED_LIGHTING

	// Per-material normal/roughness maps. Skin has no tangent stream, so the TBN is built
	// from screen derivatives. Maps live in cb_MapParams.w (1=normal, 2=rough, 3=both).
	float3 worldN        = normalize(In.WorldNormal);
	float  surfRoughness = cb_MapParams.z;
#if MATERIAL_MAPS
	if (cb_MapParams.w == 1.0f || cb_MapParams.w == 3.0f)
	{
		float3 nt = normalMap.Sample(sampler0, In.UV0).xyz * 2.0f - 1.0f;
		nt.xy    *= cb_MapParams.x; // normal strength
		worldN    = PerturbNormalDeriv(In.WorldPos, worldN, In.UV0, nt);
	}
	if (cb_MapParams.w >= 2.0f)
	{
		surfRoughness = saturate(roughnessMap.Sample(sampler0, In.UV0).r * cb_MapParams.y);
	}
#endif

	// View dir + roughness-shaped specular params, shared by the sun and dynamic highlights.
	float3 V       = normalize(cb_CameraPos.xyz - In.WorldPos); // toward the camera
	float  rough   = saturate(surfRoughness);
	float  specPow = max(lerp(8.0f, max(cb_SpecPower, 1.0f), 1.0f - rough), 1.0f);
	float  specInt = cb_SpecIntensity * (1.0f - rough);

	float3 specular = 0.0f;

#if !NO_DYN_LIGHT
	// Diffuse uses the bumpy derived normal; specular uses the clean normal-mapped worldN.
	float3 dynN    = ComputeDerivedWorldNormal(In.WorldPos, worldN, In.UV0, texture0, sampler0, cb_glowLightIntensity[0].y);
	float3 dynSpec = 0.0f;
	float3 glow    = SampleDynamicGlowLights(In.WorldPos, dynN, worldN, V, specInt, specPow, dynSpec);
	texColor.rgb   = ApplyDynamicGlowLighting(texColor.rgb, glow);
	specular      += dynSpec;
#endif

#if !NO_CSM
	float shadowStrength = cb_ShadowParams.w;
	float shadowScale = shadow * shadowStrength + (1.0f - shadowStrength);
	#if !NO_DYN_LIGHT
	shadowScale = lerp(shadowScale, 1.0f, saturate(max(glow.r, max(glow.g, glow.b))));
	#endif
	texColor.rgb *= shadowScale;
	float specShadow = shadow; // raw shadow -- a sun highlight shouldn't survive in shadow
#else
	float specShadow = 1.0f;
#endif

	// Per-material Blinn-Phong sun specular (roughness-shaped, killed in shadow).
	if (cb_SpecIntensity > 0.0f)
	{
		float3 N = worldN;
		if (dot(N, V) < 0.0f) N = -N;                         // orient to the visible side
		// Bring cb_lightDirection back to world space (see VS comment).
		float3 L = -mul(cb_lightDirection.xyz, (float3x3)cb_matModel);
		float3 H = normalize(L + V);
		float  specTerm = pow(saturate(dot(N, H)), specPow);
		specular += specTerm * specInt * specShadow * saturate(dot(N, L));
	}

    // Emissive intensity scales the texture only (specular keeps its lit magnitude).
    // Applied pre-fog so distant emissives still get fog-dimmed.
    float3 surfaceColor = texColor.xyz * cb_SSRParams.z + specular;

#if !NO_FOG
	float fogFactor = CalculateExponentialSquaredFog(In.ProjPos.w, cb_FogStart, cb_FogColor.w);
	fogFactor = saturate(fogFactor);

	// Apply fog by blending between fog color and original color
    float3 finalColor = lerp(cb_FogColor.xyz, surfaceColor, fogFactor);
#else
    float3 finalColor = surfaceColor;
#endif

    PS_OUT Out;
    Out.Color   = float4(finalColor, texColor.a);
    // G-buffer: rg = octahedral world normal (the normal-mapped one), b = reflectivity,
    // a = pack(fresnelPower, roughness). Reflectivity 0 = SSR ignores it, but the normal is
    // still written so the buffer reads correctly and reflective skin materials work.
    Out.GBuffer = float4(
        OctEncodeNormal(worldN),
        cb_SSRParams.x,
        PackFresnelRoughness(cb_SSRParams.y, surfRoughness));
    return Out;
}
