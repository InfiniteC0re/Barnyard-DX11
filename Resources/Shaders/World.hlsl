// STATIC: "ALPHAREF" "0..1" [ps]
// STATIC: "NO_CSM" "0..1" [ps]
// STATIC: "NO_FOG" "0..1" [ps]
// STATIC: "NO_DYN_LIGHT" "0..1" [ps]
// STATIC: "GLOW" "0..1" [ps]
// STATIC: "MATERIAL_MAPS" "0..1" [ps]
// STATIC: "PARALLAX" "0..1" [ps]
// STATIC: "CLOUD_SHADOWS" "0..1" [ps]
// STATIC: "WIND" "0..1" [vs]
// STATIC: "FOB" "0..1"

struct VS_IN
{
    float3 ObjPos : POSITION;
    float3 normal : NORMAL;
    float4 Color : Color;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT; // xyz = object-space tangent, w = handedness (parallel stream, slot 1)
};

struct PS_IN
{
    float4 ProjPos : SV_POSITION;
    centroid float4 Color : Color;
    centroid float2 UV0 : TEXCOORD0;
    centroid float3 WorldPos : TEXCOORD1;
    centroid float3 WorldNormal : TEXCOORD3;
    centroid float4 WorldTangent : TEXCOORD4; // xyz = world-space tangent, w = handedness
};

// Per-draw constants. Per-pass values live in PerPass.hlsli (b4); static per-material in
// MaterialCB.hlsli (b5)
cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matModel;                // 0-3 (clip position = world * pp_matViewProj)
    float4   cb_TexCoordOffsetAndAlpha;  // 4:  xy = UV offset, z = packet alpha
    float4   cb_FOBColor;                // 5:  FOB combo only: per-tree lit colour (shadow->sun blend * tint)
    float4   cb_MiscParams;              // 6:  x = isWater, y = isLit
    float4   cb_cellStaticLightIndices;  // 7:  xyzw = up to 4 static light indices into the global buffer (-1 = none)
    float4   cb_cellStaticLightParams;   // 8:  x = count
    float4   cb_cellStaticLightIndices2; // 9:  xyzw = static light indices 4..7 (-1 = none)
};

#include "PerPass.hlsli"
#include "MaterialCB.hlsli"

struct PS_OUT
{
    float4 Color   : SV_Target0;
    float4 GBuffer : SV_Target1; // rgb = world-space normal, a = reflectivity
};

// env-specular helpers, needed even when dynamic lights are compiled out
#include "ShaderUtils.hlsli"

#if !NO_CSM
#include "ShadowSampling.hlsli"
#endif

#if !NO_DYN_LIGHT
#include "DynamicLights.hlsli"
#endif

#include "StaticPointLights.hlsli"
#include "Tonemap.hlsli"
#include "GBuffer.hlsli"

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

    float3 objPos = In.ObjPos;

    // Wind repurposes the blue vertex-color channel as sway strength, so it can't feed lighting;
    // substitute green (r, g, g) to keep the baked lighting plausible
#if WIND
    float3 vtxColor = float3(In.Color.x, In.Color.y, In.Color.y);
#else
    float3 vtxColor = In.Color.xyz;
#endif

#if WIND
    // Blue channel remapped through [windMin, windMax] gives [0,1] sway strength (0 = trunk, 1 = tip).
    // Phase drifts with world XZ so the wave ripples across the surface, not in lockstep
    float  windMask  = saturate((In.Color.z - mat_Wind.x) / max(mat_Wind.y - mat_Wind.x, 1e-4f));
    float  windPhase = pp_WindParams.w + dot(objPos.xz, float2(0.35f, 0.35f));
    float  sway      = sin(windPhase) + 0.5f * sin(windPhase * 2.7f + 1.3f);
    objPos.xz += pp_WindParams.xy * (sway * pp_WindParams.z * windMask);
#endif

    Out.WorldPos = mul(float4(objPos, 1.0f), cb_matModel).xyz;
    Out.ProjPos = mul(float4(Out.WorldPos, 1.0f), pp_matViewProj);
    Out.WorldNormal = normalize(mul(In.normal, (float3x3)cb_matModel));
    Out.WorldTangent = float4(normalize(mul(In.Tangent.xyz, (float3x3)cb_matModel)), In.Tangent.w);

	// Calculate vertex Color based on current lighting settings and shadow factor (stored as vertex data)
	// FOB tree billboards blend toward the per-draw lit colour instead of the pass ambient
#if FOB
	Out.Color.xyz = lerp(pp_ShadowColor.xyz, cb_FOBColor.xyz, vtxColor);
#else
	Out.Color.xyz = lerp(pp_ShadowColor.xyz, pp_AmbientColor.xyz, vtxColor);
#endif

	// Adjust water Color
	// Water recieves less ambient Color
	Out.Color.xyz = (vtxColor * cb_MiscParams.x * 0.75f + Out.Color.xyz * 0.25f * cb_MiscParams.x) + (Out.Color.xyz * cb_MiscParams.y);

	// Calculate opacity
	// Used for water rings and probably something else
	Out.Color.w = (In.Color.x * cb_MiscParams.x) + (1.0f * cb_MiscParams.y);
	
	// Animate UV
    Out.UV0 = In.UV + cb_TexCoordOffsetAndAlpha.xy;

    return Out;
}

float CalculateExponentialSquaredFog(float distance, float fogStart, float density)
{
    if (distance <= fogStart) return 1.0f;
    return exp(-pow(density * (distance - fogStart), 2));
}

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);
Texture2D normalMap     : register(t1); // per-material tangent-space normal map
Texture2D roughnessMap  : register(t3); // per-material roughness (R channel)
Texture2D heightMap     : register(t4); // per-material height map (white = raised) for parallax
Texture2D metallicMap   : register(t8); // per-material metallic (R channel), scaled by mat_Params2.x
TextureCube skyCube     : register(t5); // active ("to") reflection cube for environment specular
TextureCube skyCube2    : register(t7); // outgoing ("from") cube, blended in during a probe switch (t6 = glow shadows)
SamplerState envSampler : register(s1); // linear-clamp sampler for the cubes
// Normal/roughness/height maps share the albedo's sampler (sampler0 @ s0), set per-material
// by WorldMaterial::PreRender, so they inherit the same wrap/clamp addressing as the diffuse.

// Surface (albedo/normal/roughness) sampling. Only the parallax path shifts the UV per-pixel,
// so only it needs explicit-gradient SampleGrad to keep mip selection stable across the POM
// march. Without parallax the UV is just the interpolated In.UV0, so a plain Sample is
// bit-identical but keeps the hardware's implicit-gradient fast path. Expands to use the
// local dUVdx/dUVdy, which only exist (and are only needed) in the PARALLAX combo.
#if PARALLAX
#define SAMPLE_SURFACE(tex, texcoord) tex.SampleGrad(sampler0, (texcoord), dUVdx, dUVdy)
#else
#define SAMPLE_SURFACE(tex, texcoord) tex.Sample(sampler0, (texcoord))
#endif

// Parallax occlusion mapping: march the height field along the tangent-space view dir and
// return the offset UV so the surface appears to have depth. a_viewTS points toward the
// viewer (z = +N). a_scale is the max displacement. Height map: white = raised.
// a_dx/a_dy are the *original-UV* screen derivatives: sampling with them (SampleGrad) keeps
// mip selection stable across the march, which is what stops the "boiling" near edges.
float2 ParallaxOcclusionUV(float2 a_uv, float3 a_viewTS, float a_scale, float2 a_dx, float2 a_dy)
{
    const int   iMaxSteps = 24;
    const float fMinLayers = 12.0f;

    // More layers at grazing angles where parallax shifts most.
    float numLayers  = lerp((float)iMaxSteps, fMinLayers, saturate(abs(a_viewTS.z)));
    float layerDepth = 1.0f / numLayers;

    // Total UV shift across the full depth; step amount per layer. Clamp the magnitude so
    // grazing angles (large viewTS.xy / viewTS.z) can't smear the UV far across the surface
    // -- this keeps the apparent depth without the trippy stretching.
    float2 P       = (a_viewTS.xy / max(a_viewTS.z, 0.001f)) * a_scale;
    float  pLen    = length(P);
    float  pMax    = a_scale * 2.0f;
    if (pLen > pMax) P *= pMax / pLen;
    float2 deltaUV = P / numLayers;

    float2 curUV    = a_uv;
    float  curDepth = 1.0f - heightMap.SampleGrad(sampler0, curUV, a_dx, a_dy).r; // height -> depth
    float  curLayer = 0.0f;

    [loop]
    for (int i = 0; i < iMaxSteps; i++)
    {
        if (curLayer >= curDepth) break;
        curUV   -= deltaUV;
        curDepth = 1.0f - heightMap.SampleGrad(sampler0, curUV, a_dx, a_dy).r;
        curLayer += layerDepth;
    }

    // Interpolate between the last two layers for a smooth intersection.
    float2 prevUV = curUV + deltaUV;
    float  after  = curDepth - curLayer;
    float  before = (1.0f - heightMap.SampleGrad(sampler0, prevUV, a_dx, a_dy).r) - (curLayer - layerDepth);
    float  w      = after / (after - before);
    return lerp(curUV, prevUV, saturate(w));
}

// Self-shadow march toward the sun. From the hit at (a_uv, a_hitDepth) we walk through
// the heightfield in tangent space; where the surface rises above the ray, the bump is
// blocking the sun. Returns 1 = fully lit, 0 = fully shadowed.
float ParallaxSelfShadow(float2 a_uv, float a_hitDepth, float3 a_lightTS, float a_scale, float2 a_dx, float2 a_dy)
{
    if (a_lightTS.z <= 0.0f) return 1.0f;

    const int   iSteps    = 8;
    const float numLayers = (float)iSteps;

    // Same grazing clamp as the view march so a low sun doesn't smear the shadow.
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
        // Earlier samples (closer to the surface) dominate, multiplied by a darkness ramp.
        float weight      = (numLayers - (float)i) / numLayers;
        occlusion         = max(occlusion, diff * weight * 4.0f);
    }

    return 1.0f - saturate(occlusion);
}

PS_OUT ps_main(PS_IN In, bool a_bFrontFace : SV_IsFrontFace)
{
    // Debug: the tangent-debug flag visualises the world-space tangent as colour. Stable colours on
    // flat ground that don't swim with the camera mean the tangents are correct
    if (pp_EnvSpecular.w > 0.5f)
    {
        PS_OUT dbg;
        dbg.Color   = float4(normalize(In.WorldTangent.xyz) * 0.5f + 0.5f, 1.0f);
        dbg.GBuffer = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return dbg;
    }

    // Tangent frame from the geometric normal + precomputed tangent (reused by parallax,
    // the normal map and the dynamic-light bump). V points toward the camera.
    float3 Ngeo = normalize(In.WorldNormal);
    float3 T    = normalize(In.WorldTangent.xyz - Ngeo * dot(Ngeo, In.WorldTangent.xyz));
    float3 B    = cross(Ngeo, T) * In.WorldTangent.w;
    float3 V    = normalize(pp_CameraPos.xyz - In.WorldPos);

    // Shared distance fade for the per-pixel detail terms (POM depth, self-shadow, and
    // the normal-map bump highlight further down). They all rely on stable mip selection
    // and small per-pixel UV variation, both of which break down past this range and
    // start to shimmer.
    float  detailFade     = 1.0f - smoothstep(15.0f, 30.0f, In.ProjPos.w);

    // Parallax occlusion mapping: shift the UV so the height map reads as depth. Gated by the
    // material's parallax scale (masked off during the reflection-cube capture); later samples use
    // this offset uv
    float2 uv             = In.UV0;
    float  parallaxShadow = 1.0f;
#if PARALLAX
    float  materialParallax = mat_MapParams.z * (1.0f - pp_EnvSpecular.z);
    // Screen-space derivatives of the *un-parallaxed* UV. The parallaxed samples use these
    // (SampleGrad) so mip selection stays stable across the POM march -- the offset UV is
    // discontinuous per pixel, and letting the GPU derive mips from it is what makes it boil.
    // Only the parallax combo needs them; the non-parallax combo uses plain Sample.
    float2 dUVdx = ddx(In.UV0);
    float2 dUVdy = ddy(In.UV0);

    // Past ~30 m detailFade hits 0, which drives the parallax scale to 0 and makes both the
    // POM and self-shadow marches a no-op -- skip the ~30 height-map fetches entirely there
    // rather than marching to no visible effect.
    if (materialParallax > 0.0f && detailFade > 0.0f)
    {
        float3 viewTS = float3(dot(V, T), dot(V, B), dot(V, Ngeo));
        // Grazing-angle ease-off so a low view doesn't smear the march across the surface.
        float  grazeFade = smoothstep(0.05f, 0.35f, viewTS.z);
        float  scale     = materialParallax * grazeFade * detailFade;
        uv = ParallaxOcclusionUV(uv, viewTS, scale, dUVdx, dUVdy);

        // Self-shadow march toward the sun. The 0.4 keeps the contribution subtle.
        // Skipped in tangent-debug mode where sun lighting is bypassed anyway.
        if (pp_EnvSpecular.w < 0.5f && scale > 0.0f)
        {
            float3 lightTS  = float3(dot(pp_SunDirection.xyz, T),
                                     dot(pp_SunDirection.xyz, B),
                                     dot(pp_SunDirection.xyz, Ngeo));
            float  hitDepth = 1.0f - heightMap.SampleGrad(sampler0, uv, dUVdx, dUVdy).r;
            float  rawSelf  = ParallaxSelfShadow(uv, hitDepth, lightTS, scale, dUVdx, dUVdy);
            parallaxShadow  = lerp(1.0f, rawSelf, detailFade * 0.4f);
        }
    }
#endif

    float4 albedo   = SAMPLE_SURFACE(texture0, uv);
    float4 texColor = albedo * In.Color * cb_TexCoordOffsetAndAlpha.z;

#if ALPHAREF
	// The only alpharef value used by the game is 128 (0.5f)
    if (texColor.a < 0.5f) discard;
#endif

    // Per-material normal/roughness/metallic maps at the parallaxed uv. Done before the dynamic
    // lights so they get the mapped normal. mapFlags packs presence bits (1 = normal, 2 = roughness,
    // 4 = metallic)
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
        float3 nt = SAMPLE_SURFACE(normalMap, uv).xyz * 2.0f - 1.0f;
        nt.xy    *= mat_MapParams.x; // normal-map strength
        worldN    = normalize(T * nt.x + B * nt.y + Ngeo * nt.z);
    }
    if (mapFlags & 2)
    {
        surfRoughness = saturate(SAMPLE_SURFACE(roughnessMap, uv).r * mat_MapParams.y);
    }
    if (mapFlags & 4)
    {
        metallic = saturate(SAMPLE_SURFACE(metallicMap, uv).r * metallicScale);
    }
#endif

    // Roughness-shaped specular params shared by the dynamic-light and sun highlights
    // (smooth = tight & bright, rough = broad & dim, none at full roughness). The rough
    // end is clamped below the material's specularPower so materials authored with a
    // narrow lobe (specularPower < 8) don't end up tighter at high roughness than at low.
    float  rough        = saturate(surfRoughness);
    float  roughSpecPow = min(mat_Reflectivity.w, 8.0f);
    float  specPow      = max(lerp(roughSpecPow, mat_Reflectivity.w, 1.0f - rough), 1.0f);
    float  specInt      = mat_Reflectivity.z * (1.0f - rough);

    // Metallic (Blinn-Phong): metals tint highlights with albedo and lose most diffuse; the env
    // reflection below uses albedo as F0. metallic = 0 (default) leaves the old shading unchanged
    float3 specColor = lerp(1.0f.xxx, albedo.rgb, metallic);
    texColor.rgb    *= 1.0f - 0.6f * metallic;

    float3 specular = 0.0f;

#if !NO_DYN_LIGHT
	// Dynamic glow lights use the real normal-mapped normal, still perturbed by the cheap
	// albedo-luminance micro-bump for extra detail, and now contribute specular too.
	float3 dynN    = ComputeDerivedWorldNormalT(worldN, In.WorldTangent.xyz, In.WorldTangent.w, uv, texture0, sampler0, cb_glowLightIntensity[0].y);
	float3 dynSpec = 0.0f;
	// Diffuse uses the bumpy derived normal (dynN); specular uses the clean worldN so the
	// highlight is a crisp glint that tracks the light, not a broad smear over the surface.
	float3 glow    = SampleDynamicGlowLights(In.WorldPos, dynN, worldN, V, specInt, specPow, dynSpec);
	specular      += dynSpec * specColor;
#endif

    // Point lights (static + dynamic glow) applied additively on the raw albedo below, after the
    // sun shadow: extra incoming light neither scales with the baked shading nor dims in shadow
    float3 pointLight = SampleStaticPointLights(In.WorldPos, worldN, cb_cellStaticLightIndices, cb_cellStaticLightIndices2, (int)cb_cellStaticLightParams.x);
#if !NO_DYN_LIGHT
    pointLight += glow;
#endif

#if !NO_CSM
    float shadow = SampleShadow(In.WorldPos, In.WorldNormal, In.ProjPos.w);
    float shadowStrength = cb_ShadowParams.w;
    float shadowScale = shadow * shadowStrength + (1.0f - shadowStrength);
    // Specular uses the *raw* shadow (no ambient floor): a sun highlight shouldn't survive
    // where the sun is occluded, even though diffuse keeps the lifted shadow for art.
    float specShadow = shadow;
#else
    float shadowScale = 1.0f;
    float specShadow  = 1.0f;
#endif

    // FOB tree billboards: lighting is fully baked into the CPU shadow->lit vertex-colour blend and
    // the quad normals are fake, so the sun-driven per-pixel terms compile out
#if !FOB
    // Normal-map detail shading: add only the *delta* in sun lambert caused by the bump,
    // so it sculpts the surface without double-counting the game's baked vertex lighting.
    // Zero when there's no normal map (worldN == Ngeo); gated by shadow (it's sun-driven)
    // and by detailFade so distant micro-bumps don't shimmer once their mips can't keep up.
    {
        float bumpDelta = saturate(dot(worldN, pp_SunDirection.xyz)) - saturate(dot(Ngeo, pp_SunDirection.xyz));
        texColor.rgb *= clamp(1.0f + bumpDelta * 1.5f * shadowScale * parallaxShadow * detailFade, 0.0f, 2.0f);
    }

    // Crevice darkening from the parallax self-shadow. 0.75 = floor brightness.
    texColor.rgb *= lerp(0.75f, 1.0f, parallaxShadow);

    // Per-material Blinn-Phong sun specular (roughness-shaped, killed in shadow).
    if (mat_Reflectivity.z > 0.0f)
    {
        float3 N = worldN;
        if (dot(N, V) < 0.0f) N = -N;
        float3 L = pp_SunDirection.xyz;                       // toward the sun
        float3 H = normalize(L + V);
        float  specTerm = pow(saturate(dot(N, H)), specPow);
        specular += specTerm * specInt * specShadow * saturate(dot(N, L)) * parallaxShadow * specColor;
    }
#endif

    // Cubemap env specular (IBL): reflects the captured sky/terrain, roughness-blurred via mips.
    // Masked by material specular intensity, off during the cube capture. Not shadowed; SSR layers
    // sharp reflections on top in post
#if !FOB
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
#endif

    // Firefly clamp: at mirror angles the env cube (HDR sun) or a tight highlight can spike one pixel
    // high enough for bloom to smear it into a "butterfly". Normal highlights sit well below 8
    specular = min( specular, 8.0f );

    // Emissive intensity scales the texture only (specular keeps its lit magnitude).
    // Applied pre-fog so distant emissives still get fog-dimmed.
    float3 surfaceColor = texColor.xyz * mat_Params2.w * shadowScale + specular;
    surfaceColor += albedo.rgb * cb_TexCoordOffsetAndAlpha.z * ( 1.0f - 0.6f * metallic ) * mat_Params2.w * pointLight;

#if FOB
    // Sun subsurface for foliage billboards: looking toward the sun through the canopy makes
    // the leaves glow through, tinted by the per-tree lit colour. Gated by the raw sun shadow
    // so occluded canopies don't glow
    {
        float sunTrans = pow(saturate(dot(-V, pp_SunDirection.xyz)), 8.0f);
        surfaceColor  += albedo.rgb * cb_TexCoordOffsetAndAlpha.z * cb_FOBColor.xyz * ( sunTrans * 0.35f * specShadow );
    }
#endif

#if !NO_FOG
	float fogFactor = CalculateExponentialSquaredFog(In.ProjPos.w, pp_FogParams.x, pp_FogColor.w);
	fogFactor = saturate(fogFactor);
    // Apply fog by blending between fog color and original color
    float3 finalColor = lerp(pp_FogColor.xyz, surfaceColor, fogFactor);
#else
    float3 finalColor = surfaceColor;
#endif

#if GLOW
    finalColor *= float3(255, 252, 204) / 255.0f;
#endif

    PS_OUT Out;
    Out.Color = float4(finalColor, texColor.a);
    // G-buffer: rg = octahedral normal, b = reflectivity, a = pack(fresnelPower, roughness).
    Out.GBuffer = float4(
        OctEncodeNormal(worldN),
        mat_Reflectivity.x,
        PackFresnelRoughness(mat_Reflectivity.y, surfRoughness));
    return Out;
}
