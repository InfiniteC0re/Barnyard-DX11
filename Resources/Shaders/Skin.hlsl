// STATIC: "BAKED_LIGHTING" "0..1"
// STATIC: "NO_CSM" "0..1" [ps]
// STATIC: "NO_FOG" "0..1" [ps]
// STATIC: "NO_DYN_LIGHT" "0..1" [ps]
// STATIC: "ANIMATED" "0..1" [vs]
// STATIC: "MATERIAL_MAPS" "0..1" [ps]
// STATIC: "CLOUD_SHADOWS" "0..1" [ps]
// STATIC: "WIND" "0..1" [vs]
// STATIC: "PARALLAX" "0..1" [ps]

struct VS_IN
{
	float3 ObjPos	: POSITION;		// Object space position
    float3 Normal	: NORMAL;		// Vertex normal
	float4 Weights	: BLENDWEIGHT;	// Weights
	float4 MIndices	: BLENDINDICES;	// Matrix Indices
	float2 UV		: TEXCOORD0;	// UV
	float4 Tangent	: TANGENT;		// xyz = object-space tangent, w = handedness
};

struct PS_IN
{
	float4 ProjPos	: SV_POSITION;		// Projected space position
	// float3 Normal	: NORMAL;
	centroid float2 UV0		: TEXCOORD0;		// UV
	// float4 ViewPos	: TEXCOORD3;	// View space position
	// float FogFactor	: FOG;
#if BAKED_LIGHTING
	centroid float2 UV1		: TEXCOORD1;		// UV for baked lighting
	centroid float4 LightingLerp1 : TEXCOORD5;
	centroid float4 LightingLerp2 : TEXCOORD6;
#else // BAKED_LIGHTING
	centroid float4 Color	: COLOR;        // xyz = ambient, w = alpha
	centroid float3 DirectLight : TEXCOORD8; // NdotL * lightColor -- the sun term, gated by shadow in the PS
#endif // !BAKED_LIGHTING
	centroid float3 WorldPos    : TEXCOORD2;
	centroid float AlphaRef     : TEXCOORD4;
	centroid float3 WorldNormal : TEXCOORD7;
	centroid float4 WorldTangent : TEXCOORD9; // xyz = world-space tangent, w = handedness
};

// Per-draw constants. Per-pass values live in PerPass.hlsli (b4); static per-material in
// MaterialCB.hlsli (b5)
cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matModel;                // 0-3 (clip position = world * pp_matViewProj)
    float4   cb_ambientColor;            // 4:  per-packet ambient, w = packet alpha
    float4   cb_lightColor;              // 5:  xyz = light color, w = alpha ref
    float4   cb_lightDirection;          // 6:  per-packet light dir (inverse-model space)
    float4   cb_upAxis;                  // 7
    float4   cb_lightingLerp;            // 8
    float4   cb_cellStaticLightIndices;  // 9:  xyzw = up to 4 static light indices into the global buffer (-1 = none)
    float4   cb_cellStaticLightParams;   // 10: x = count
    float4   cb_cellStaticLightIndices2; // 11: xyzw = static light indices 4..7 (-1 = none)
};

#include "PerPass.hlsli"
#include "MaterialCB.hlsli"

#ifdef ANIMATED

// Packed bone palette: 3 registers per bone. Explicit column_major (under /Zpr a row_major float4x3
// takes 4 registers): register j = column j of the bone transform, transposed on the CPU (48 bytes/bone)
cbuffer BoneCBuffer : register(b1)
{
    column_major float4x3 cb_bones[28];
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

#include "StaticPointLights.hlsli"
#include "ShaderUtils.hlsli" // ComputeDerivedWorldNormalT, EnvSpecular
#include "GBuffer.hlsli"     // OctEncodeNormal / PackFresnelRoughness for SSR

// Declared before vs_main so the wind path can sample the roughness map in the vertex stage
Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);
Texture2D normalMap    : register(t7); // per-material normal map (t1-t4 are baked lighting)
Texture2D roughnessMap : register(t8); // per-material roughness (R = roughness, B = wind strength)
Texture2D heightMap    : register(t10); // per-material height map (white = raised) for parallax
Texture2D metallicMap  : register(t13); // per-material metallic (R channel), scaled by mat_Params2.x
TextureCube skyCube     : register(t11); // active ("to") reflection cube for environment specular
TextureCube skyCube2    : register(t12); // outgoing ("from") cube, blended in during a probe switch
SamplerState envSampler : register(s4);  // linear-clamp sampler for the cubes

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
	float3 tangent = 0;
	for (int i = 0; i < 4; ++i)
	{
		float4x3 BoneMatrix = cb_bones[BoneIndices[i]];
		vertex += mul(float4(In.ObjPos, 1.0), BoneMatrix) * BoneWeights[i];

		float3x3 BoneNormal = (float3x3)BoneMatrix;
		normal += mul(In.Normal, BoneNormal) * BoneWeights[i];
		tangent += mul(In.Tangent.xyz, BoneNormal) * BoneWeights[i];
	}

#else // ANIMATED

	float3 vertex = In.ObjPos;
	float3 normal = In.Normal;
	float3 tangent = In.Tangent.xyz;

#endif // !ANIMATED

#if WIND
    // Skin has no vertex color, so wind strength is the roughness map's blue channel (mip 0 -- no
    // gradients in the VS), remapped through [windMin, windMax] and driven by the same two-sine sway
    float  windBlue  = roughnessMap.SampleLevel(sampler0, In.UV, 0).b;
    float  windMask  = saturate((windBlue - mat_Wind.x) / max(mat_Wind.y - mat_Wind.x, 1e-4f));
    float  windPhase = pp_WindParams.w + dot(vertex.xz, float2(0.35f, 0.35f));
    float  sway      = sin(windPhase) + 0.5f * sin(windPhase * 2.7f + 1.3f);
    vertex.xz += pp_WindParams.xy * (sway * pp_WindParams.z * windMask);
#endif

	Out.WorldPos = mul(float4(vertex, 1.0), cb_matModel).xyz;
	Out.ProjPos = mul(float4(Out.WorldPos, 1.0), pp_matViewProj);
	Out.AlphaRef = cb_lightColor.w;
	Out.WorldNormal = normalize(mul(normal, (float3x3)cb_matModel));

	float3 worldT = mul(tangent, (float3x3)cb_matModel);
	Out.WorldTangent = float4(worldT * rsqrt(max(dot(worldT, worldT), 1e-8f)), In.Tangent.w);
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

#else // !BAKED_LIGHTING

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

float CalculateExponentialSquaredFog(float distance, float fogStart, float density)
{
    if (distance <= fogStart) return 1.0f;
    return exp(-pow(density * (distance - fogStart), 2));
}

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

// Only the parallax path shifts UV per-pixel, so only it needs SampleGrad to keep mips stable
// across the POM march. Without parallax it's a plain Sample
#if PARALLAX
#define SAMPLE_SKIN(tex, texcoord) tex.SampleGrad(sampler0, (texcoord), dUVdx, dUVdy)
#else
#define SAMPLE_SKIN(tex, texcoord) tex.Sample(sampler0, (texcoord))
#endif

#if PARALLAX
// Parallax occlusion mapping: march the height field along the tangent-space view dir, return the
// offset UV. SampleGrad with original-UV derivatives keeps mips stable. Height map: white = raised.
// Ported from World.hlsl
float2 ParallaxOcclusionUV(float2 a_uv, float3 a_viewTS, float a_scale, float2 a_dx, float2 a_dy)
{
    const int   iMaxSteps  = 24;
    const float fMinLayers = 12.0f;

    float numLayers  = lerp((float)iMaxSteps, fMinLayers, saturate(abs(a_viewTS.z)));
    float layerDepth = 1.0f / numLayers;

    float2 P    = (a_viewTS.xy / max(a_viewTS.z, 0.001f)) * a_scale;
    float  pLen = length(P);
    float  pMax = a_scale * 2.0f;
    if (pLen > pMax) P *= pMax / pLen;
    float2 deltaUV = P / numLayers;

    float2 curUV    = a_uv;
    float  curDepth = 1.0f - heightMap.SampleGrad(sampler0, curUV, a_dx, a_dy).r;
    float  curLayer = 0.0f;

    [loop]
    for (int i = 0; i < iMaxSteps; i++)
    {
        if (curLayer >= curDepth) break;
        curUV   -= deltaUV;
        curDepth = 1.0f - heightMap.SampleGrad(sampler0, curUV, a_dx, a_dy).r;
        curLayer += layerDepth;
    }

    float2 prevUV = curUV + deltaUV;
    float  after  = curDepth - curLayer;
    float  before = (1.0f - heightMap.SampleGrad(sampler0, prevUV, a_dx, a_dy).r) - (curLayer - layerDepth);
    float  w      = after / (after - before);
    return lerp(curUV, prevUV, saturate(w));
}
float ParallaxSelfShadow(float2 a_uv, float a_hitDepth, float3 a_lightTS, float a_scale, float2 a_dx, float2 a_dy)
{
    if (a_lightTS.z <= 0.0f) return 1.0f;

    const int   iSteps    = 8;
    const float numLayers = (float)iSteps;

    float2 dirTS  = (a_lightTS.xy / max(a_lightTS.z, 0.001f)) * a_scale;
    float  dirLen = length(dirTS);
    if (dirLen > a_scale * 2.0f) dirTS *= (a_scale * 2.0f) / dirLen;

    float  layerDepth = 1.0f / numLayers;
    float2 deltaUV    = dirTS / numLayers;

    float2 curUV     = a_uv;
    float  curDepth  = a_hitDepth;
    float  occlusion = 0.0f;

    [loop]
    for (int i = 0; i < iSteps; i++)
    {
        curUV    += deltaUV;
        curDepth -= layerDepth;
        if (curDepth <= 0.0f) break;

        float sampleDepth = 1.0f - heightMap.SampleGrad(sampler0, curUV, a_dx, a_dy).r;
        float diff        = max(0.0f, curDepth - sampleDepth);
        float weight      = (numLayers - (float)i) / numLayers;
        occlusion         = max(occlusion, diff * weight * 4.0f);
    }

    return 1.0f - saturate(occlusion);
}
#endif // PARALLAX

PS_OUT ps_main(PS_IN In, bool a_bFrontFace : SV_IsFrontFace)
{
    float2 uv = In.UV0;
    float  parallaxShadow = 1.0f;

    float3 Ngeo = normalize(In.WorldNormal);
    float3 Traw = In.WorldTangent.xyz - Ngeo * dot(Ngeo, In.WorldTangent.xyz);
    float3 T    = Traw * rsqrt(max(dot(Traw, Traw), 1e-8f));
    float3 B    = cross(Ngeo, T) * In.WorldTangent.w;
    float3 V    = normalize(pp_CameraPos.xyz - In.WorldPos);
    float  detailFade    = 1.0f - smoothstep(15.0f, 30.0f, In.ProjPos.w);
    float3 worldLightDir = -mul(cb_lightDirection.xyz, (float3x3)cb_matModel);

#if PARALLAX
    float2 dUVdx = ddx(In.UV0);
    float2 dUVdy = ddy(In.UV0);
    // Parallax scale masked off during the reflection-cube capture (not worth the march there)
    float  materialParallax = mat_MapParams.z * (1.0f - pp_EnvSpecular.z);
    if (materialParallax > 0.0f && detailFade > 0.0f)
    {
        float3 viewTS    = float3(dot(V, T), dot(V, B), dot(V, Ngeo));
        float  grazeFade = smoothstep(0.05f, 0.35f, viewTS.z);
        float  scale     = materialParallax * grazeFade * detailFade;
        uv = ParallaxOcclusionUV(uv, viewTS, scale, dUVdx, dUVdy);

        if (scale > 0.0f)
        {
            float3 lightTS  = float3(dot(worldLightDir, T), dot(worldLightDir, B), dot(worldLightDir, Ngeo));
            float  hitDepth = 1.0f - heightMap.SampleGrad(sampler0, uv, dUVdx, dUVdy).r;
            float  rawSelf  = ParallaxSelfShadow(uv, hitDepth, lightTS, scale, dUVdx, dUVdy);
            parallaxShadow  = lerp(1.0f, rawSelf, detailFade * 0.4f);
        }
    }
#endif

    float4 albedo   = SAMPLE_SKIN(texture0, uv); // raw albedo, kept for the metallic tint
    float4 texColor = albedo;
	clip(texColor.a - In.AlphaRef);

	// Sun shadow visibility, up front so the directional lighting term can be gated by it.
#if !NO_CSM
	float shadow = SampleShadow(In.WorldPos, In.WorldNormal, In.ProjPos.w);
	// Terminator clamp: a surface facing away from the sun cannot be sunlit, so cap the
	// shadow term by sun-facing-ness (fade completes at NdotL ~0.25). Kills shadow-map
	// acne on curved skinned surfaces, where depth precision fights right at the
	// terminator and the PCF compare flickers per texel. Uses the geometric normal on
	// purpose: normal-mapped bumps must not un-shadow past the terminator
	float sunFacing = saturate(dot(Ngeo, pp_SunDirection.xyz) * 4.0f);
	shadow = min(shadow, sunFacing);
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
	// Ambient (In.Color) always; the directional sun term is gated by shadow so it vanishes in shade
	texColor.rgb = texColor.rgb * (In.Color.rgb + In.DirectLight * shadow);
	texColor.a *= cb_ambientColor.a;
#endif // !BAKED_LIGHTING

	// Per-material normal/roughness/metallic maps. mat_Wind.z packs presence bits
	// (1 = normal, 2 = rough, 4 = metallic)
	int    mapFlags      = (int)mat_Wind.z;
	float3 worldN        = Ngeo;
	float  surfRoughness = mat_MapParams.w;
	// Metallic masked off during the reflection-cube capture, or metals' softened diffuse bakes dark
	// into the cube they'll later reflect
	float  metallicScale = mat_Params2.x * (1.0f - pp_EnvSpecular.z);
	float  metallic      = saturate(metallicScale);
#if MATERIAL_MAPS
	if (mapFlags & 1)
	{
		float3 nt = SAMPLE_SKIN(normalMap, uv).xyz * 2.0f - 1.0f;
		nt.xy    *= mat_MapParams.x; // normal strength
		worldN    = normalize(T * nt.x + B * nt.y + Ngeo * nt.z);
	}
	if (mapFlags & 2)
	{
		surfRoughness = saturate(SAMPLE_SKIN(roughnessMap, uv).r * mat_MapParams.y);
	}
	if (mapFlags & 4)
	{
		metallic = saturate(SAMPLE_SKIN(metallicMap, uv).r * metallicScale);
	}
#endif

	// Normal-map detail shading: add only the bump's delta in sun lambert, to avoid double-counting
	// the VS-computed lighting
#if MATERIAL_MAPS && !BAKED_LIGHTING
	{
		float bumpDelta = saturate(dot(worldN, worldLightDir)) - saturate(dot(Ngeo, worldLightDir));
		texColor.rgb *= clamp(1.0f + bumpDelta * 1.5f * shadow * parallaxShadow * detailFade, 0.0f, 2.0f);
	}
#endif

	texColor.rgb *= lerp(0.75f, 1.0f, parallaxShadow);

	// Roughness-shaped specular params. Clamp the rough end below the material's specularPower (as in
	// World) so a narrow-lobe material (specularPower < 8) isn't tighter at high roughness than at low
	float  rough        = saturate(surfRoughness);
	float  matSpecPow   = max(mat_Reflectivity.w, 1.0f);
	float  roughSpecPow = min(matSpecPow, 8.0f);
	float  specPow      = max(lerp(roughSpecPow, matSpecPow, 1.0f - rough), 1.0f);
	float  specInt      = mat_Reflectivity.z * (1.0f - rough);

	// Metallic (Blinn-Phong): metals tint highlights with albedo and lose most diffuse; the env
	// reflection below uses albedo as F0. metallic = 0 (default) leaves the old shading unchanged
	float3 specColor = lerp(1.0f.xxx, albedo.rgb, metallic);
	texColor.rgb    *= 1.0f - 0.6f * metallic;

	float3 specular = 0.0f;

#if !NO_DYN_LIGHT
	// Diffuse uses the bumpy derived normal; specular uses the clean normal-mapped worldN.
	float3 dynN    = ComputeDerivedWorldNormalT(worldN, In.WorldTangent.xyz, In.WorldTangent.w, uv, texture0, sampler0, cb_glowLightIntensity[0].y);
	float3 dynSpec = 0.0f;
	float3 glow    = SampleDynamicGlowLights(In.WorldPos, dynN, worldN, V, specInt, specPow, dynSpec);
	texColor.rgb  += albedo.rgb * glow; // additive, matching the static lights below
	specular      += dynSpec * specColor;
#endif

	// Additive on raw albedo: a point light is extra incoming light, so it must not scale with
	// the sun/baked shading -- multiplying (1 + s) into the shaded color lit the sun-facing side
	// far more than the side actually facing the light
	float3 staticLight = SampleStaticPointLights(In.WorldPos, worldN, cb_cellStaticLightIndices, cb_cellStaticLightIndices2, (int)cb_cellStaticLightParams.x);
	texColor.rgb += albedo.rgb * staticLight;

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

	// Per-material Blinn-Phong sun specular (killed in shadow). Uses the CSM sun direction (same as
	// the shadows and World) so the highlight can't point a different way than the shadow gating it.
	// Diffuse still uses the vanilla per-packet light dir (VS)
	if (mat_Reflectivity.z > 0.0f)
	{
		float3 N = worldN;
		if (dot(N, V) < 0.0f) N = -N;                         // orient to the visible side
		float3 L = pp_SunDirection.xyz;                       // toward the sun (world, CSM)
		float3 H = normalize(L + V);
		float  specTerm = pow(saturate(dot(N, H)), specPow);
		specular += specTerm * specInt * specShadow * saturate(dot(N, L)) * parallaxShadow * specColor;
	}

	// Cubemap env specular (IBL): reflects the captured sky/terrain, roughness-blurred. Masked by the
	// material specular intensity; box-parallax-corrected like SSR
	if (pp_EnvSpecular.x > 0.0f && mat_Params2.y > 0.0f)
	{
		// relPos is relative to the capture probe centre, not the camera, or the reflection swims.
		// Active ("to") cube first; during a probe switch cross-fade with the outgoing ("from") cube,
		// each with its own probe + box. Metals use albedo as F0; dielectrics keep specularF0
		float3 envF0 = lerp(mat_Params2.zzz, albedo.rgb, metallic);
		float3 relTo = In.WorldPos - pp_EnvProbePos.xyz;
		float3 env   = EnvSpecular(skyCube, envSampler, worldN, V, relTo,
		                           surfRoughness, pp_EnvSpecular.y, envF0,
		                           pp_EnvParallax.xyz);
		float blend = pp_EnvProbePos.w;
		if (blend < 0.999f)
		{
			float3 relFrom = In.WorldPos - pp_EnvProbePos2.xyz;
			float3 envFrom = EnvSpecular(skyCube2, envSampler, worldN, V, relFrom,
			                             surfRoughness, pp_EnvSpecular.y, envF0,
			                             pp_EnvParallax2.xyz);
			env = lerp(envFrom, env, blend);
		}
		specular += env * pp_EnvSpecular.x * mat_Params2.y;
	}

    // Firefly clamp (see World.hlsl): caps one-pixel reflection spikes that bloom into "butterflies";
    // normal highlights are far below this
    specular = min( specular, 8.0f );

    // Emissive intensity scales the texture only (specular keeps its lit magnitude).
    // Applied pre-fog so distant emissives still get fog-dimmed.
    float3 surfaceColor = texColor.xyz * mat_Params2.w + specular;

#if !NO_FOG
	float fogFactor = CalculateExponentialSquaredFog(In.ProjPos.w, pp_FogParams.x, pp_FogColor.w);
	fogFactor = saturate(fogFactor);

	// Apply fog by blending between fog color and original color
    float3 finalColor = lerp(pp_FogColor.xyz, surfaceColor, fogFactor);
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
        mat_Reflectivity.x,
        PackFresnelRoughness(mat_Reflectivity.y, surfRoughness));
    return Out;
}
