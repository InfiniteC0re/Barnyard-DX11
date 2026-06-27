// STATIC: "ALPHAREF" "0..1"
// STATIC: "NO_CSM" "0..1"
// STATIC: "NO_FOG" "0..1"
// STATIC: "NO_DYN_LIGHT" "0..1"
// STATIC: "GLOW" "0..1"
// STATIC: "MATERIAL_MAPS" "0..1"
// STATIC: "PARALLAX" "0..1"
// STATIC: "CLOUD_SHADOWS" "0..1"

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
    float4 Color : Color;
    float2 UV0 : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
    float ViewDepth : TEXCOORD2;
    float3 WorldNormal : TEXCOORD3;
    float4 WorldTangent : TEXCOORD4; // xyz = world-space tangent, w = handedness
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matMVP;
	float4   cb_TexCoordOffsetAndAlpha;
	float4   cb_AmbientColor;
	float4   cb_ShadowColor;
	float    cb_IsWater;
	float    cb_IsLit;
	float    cb_FogStart;
	float    cb_FogEnd;
	float4   cb_FogColor;
    float4x4 cb_matModel;
    float4   cb_Reflectivity; // x = SSR reflectivity, y = fresnel power, z = specular intensity, w = specular power (slot 13)
    float4   cb_SunDirection; // xyz = direction toward the sun (world), w = SSR roughness (slot 14)
    float4   cb_CameraPos;    // xyz = camera world position, w = map flags (slot 15)
    float4   cb_MapParams;    // x = normal-map strength, y = roughness-map strength, z = parallax scale, w = emissive intensity (slot 16)
};

struct PS_OUT
{
    float4 Color   : SV_Target0;
    float4 GBuffer : SV_Target1; // rgb = world-space normal, a = reflectivity
};

#if !NO_CSM
#include "ShadowSampling.hlsli"
#endif

#if !NO_DYN_LIGHT
#include "DynamicLights.hlsli"
#endif

#include "Tonemap.hlsli"
#include "GBuffer.hlsli"

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

	// Calculate vertex screen position
    Out.ProjPos = mul(float4(In.ObjPos, 1.0f), cb_matMVP);
    Out.WorldPos = mul(float4(In.ObjPos, 1.0f), cb_matModel).xyz;
    Out.ViewDepth = Out.ProjPos.w;
    Out.WorldNormal = normalize(mul(In.normal, (float3x3)cb_matModel));
    Out.WorldTangent = float4(normalize(mul(In.Tangent.xyz, (float3x3)cb_matModel)), In.Tangent.w);

	// Calculate vertex Color based on current lighting settings and shadow factor (stored as vertex data)
	Out.Color.xyz = lerp(cb_ShadowColor.xyz, cb_AmbientColor.xyz, In.Color.xyz);
    
	// Adjust water Color
	// Water recieves less ambient Color
	Out.Color.xyz = (In.Color.xyz * cb_IsWater * 0.75f + Out.Color.xyz * 0.25f * cb_IsWater) + (Out.Color.xyz * cb_IsLit);

	// Calculate opacity
	// Used for water rings and probably something else
	Out.Color.w = (In.Color.x * cb_IsWater) + (1.0f * cb_IsLit);
	
	// Animate UV
    Out.UV0 = In.UV + cb_TexCoordOffsetAndAlpha.xy;

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
Texture2D normalMap     : register(t1); // per-material tangent-space normal map
Texture2D roughnessMap  : register(t3); // per-material roughness (R channel)
Texture2D heightMap     : register(t4); // per-material height map (white = raised) for parallax
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
    // Debug: negative roughness sentinel (set by the CPU when tangent debugging is on)
    // visualises the precomputed world-space tangent as colour. Stable colours on flat
    // ground that don't swim with the camera mean the tangents are correct world-space.
    if (cb_SunDirection.w < -0.5f)
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
    float3 V    = normalize(cb_CameraPos.xyz - In.WorldPos);

    // Shared distance fade for the per-pixel detail terms (POM depth, self-shadow, and
    // the normal-map bump highlight further down). They all rely on stable mip selection
    // and small per-pixel UV variation, both of which break down past this range and
    // start to shimmer.
    float  detailFade     = 1.0f - smoothstep(15.0f, 30.0f, In.ViewDepth);

    // Parallax occlusion mapping: shift the UV so the height map reads as apparent depth.
    // Gated by parallaxScale (cb_MapParams.z > 0); every later sample uses this offset uv.
    float2 uv             = In.UV0;
    float  parallaxShadow = 1.0f;
#if PARALLAX
    // Screen-space derivatives of the *un-parallaxed* UV. The parallaxed samples use these
    // (SampleGrad) so mip selection stays stable across the POM march -- the offset UV is
    // discontinuous per pixel, and letting the GPU derive mips from it is what makes it boil.
    // Only the parallax combo needs them; the non-parallax combo uses plain Sample.
    float2 dUVdx = ddx(In.UV0);
    float2 dUVdy = ddy(In.UV0);

    // Past ~30 m detailFade hits 0, which drives the parallax scale to 0 and makes both the
    // POM and self-shadow marches a no-op -- skip the ~30 height-map fetches entirely there
    // rather than marching to no visible effect.
    if (cb_MapParams.z > 0.0f && detailFade > 0.0f)
    {
        float3 viewTS = float3(dot(V, T), dot(V, B), dot(V, Ngeo));
        // Grazing-angle ease-off so a low view doesn't smear the march across the surface.
        float  grazeFade = smoothstep(0.05f, 0.35f, viewTS.z);
        float  scale     = cb_MapParams.z * grazeFade * detailFade;
        uv = ParallaxOcclusionUV(uv, viewTS, scale, dUVdx, dUVdy);

        // Self-shadow march toward the sun. The 0.4 keeps the contribution subtle.
        // Skipped in tangent-debug mode where sun lighting is bypassed anyway.
        if (cb_SunDirection.w >= -0.5f && scale > 0.0f)
        {
            float3 lightTS  = float3(dot(cb_SunDirection.xyz, T),
                                     dot(cb_SunDirection.xyz, B),
                                     dot(cb_SunDirection.xyz, Ngeo));
            float  hitDepth = 1.0f - heightMap.SampleGrad(sampler0, uv, dUVdx, dUVdy).r;
            float  rawSelf  = ParallaxSelfShadow(uv, hitDepth, lightTS, scale, dUVdx, dUVdy);
            parallaxShadow  = lerp(1.0f, rawSelf, detailFade * 0.4f);
        }
    }
#endif

    float4 texColor = SAMPLE_SURFACE(texture0, uv) * In.Color * cb_TexCoordOffsetAndAlpha.z;

#if ALPHAREF
	// The only alpharef value used by the game is 128 (0.5f)
    if (texColor.a < 0.5f) discard;
#endif

    // Per-material normal/roughness maps (sampled at the parallaxed uv), reusing T/B/Ngeo.
    // Computed before the dynamic lights so they can use the real normal-mapped normal.
    float3 worldN        = Ngeo;
    float  surfRoughness = cb_SunDirection.w;
#if MATERIAL_MAPS
    if (cb_CameraPos.w == 1.0f || cb_CameraPos.w == 3.0f)
    {
        float3 nt = SAMPLE_SURFACE(normalMap, uv).xyz * 2.0f - 1.0f;
        nt.xy    *= cb_MapParams.x; // normal-map strength (scales the tangent-space tilt)
        worldN    = normalize(T * nt.x + B * nt.y + Ngeo * nt.z);
    }
    if (cb_CameraPos.w >= 2.0f)
    {
        surfRoughness = saturate(SAMPLE_SURFACE(roughnessMap, uv).r * cb_MapParams.y);
    }
#endif

    // Roughness-shaped specular params shared by the dynamic-light and sun highlights
    // (smooth = tight & bright, rough = broad & dim, none at full roughness). The rough
    // end is clamped below the material's specularPower so materials authored with a
    // narrow lobe (specularPower < 8) don't end up tighter at high roughness than at low.
    float  rough        = saturate(surfRoughness);
    float  roughSpecPow = min(cb_Reflectivity.w, 8.0f);
    float  specPow      = max(lerp(roughSpecPow, cb_Reflectivity.w, 1.0f - rough), 1.0f);
    float  specInt      = cb_Reflectivity.z * (1.0f - rough);

    float3 specular = 0.0f;

#if !NO_DYN_LIGHT
	// Dynamic glow lights use the real normal-mapped normal, still perturbed by the cheap
	// albedo-luminance micro-bump for extra detail, and now contribute specular too.
	float3 dynN    = ComputeDerivedWorldNormalT(worldN, In.WorldTangent.xyz, In.WorldTangent.w, uv, texture0, sampler0, cb_glowLightIntensity[0].y);
	float3 dynSpec = 0.0f;
	// Diffuse uses the bumpy derived normal (dynN); specular uses the clean worldN so the
	// highlight is a crisp glint that tracks the light, not a broad smear over the surface.
	float3 glow    = SampleDynamicGlowLights(In.WorldPos, dynN, worldN, V, specInt, specPow, dynSpec);
	texColor.rgb   = ApplyDynamicGlowLighting(texColor.rgb, glow);
	specular      += dynSpec;
#endif

#if !NO_CSM
    float shadow = SampleShadow(In.WorldPos, In.WorldNormal, In.ViewDepth);
    float shadowStrength = cb_ShadowParams.w;
    float shadowScale = shadow * shadowStrength + (1.0f - shadowStrength);
    // Specular uses the *raw* shadow (no ambient floor): a sun highlight shouldn't survive
    // where the sun is occluded, even though diffuse keeps the lifted shadow for art.
    float specShadow = shadow;
#else
    float shadowScale = 1.0f;
    float specShadow  = 1.0f;
#endif

    // Normal-map detail shading: add only the *delta* in sun lambert caused by the bump,
    // so it sculpts the surface without double-counting the game's baked vertex lighting.
    // Zero when there's no normal map (worldN == Ngeo); gated by shadow (it's sun-driven)
    // and by detailFade so distant micro-bumps don't shimmer once their mips can't keep up.
    {
        float bumpDelta = saturate(dot(worldN, cb_SunDirection.xyz)) - saturate(dot(Ngeo, cb_SunDirection.xyz));
        texColor.rgb *= clamp(1.0f + bumpDelta * 1.5f * shadowScale * parallaxShadow * detailFade, 0.0f, 2.0f);
    }

    // Crevice darkening from the parallax self-shadow. 0.75 = floor brightness.
    texColor.rgb *= lerp(0.75f, 1.0f, parallaxShadow);

    // Per-material Blinn-Phong sun specular (roughness-shaped, killed in shadow).
    if (cb_Reflectivity.z > 0.0f)
    {
        float3 N = worldN;
        if (dot(N, V) < 0.0f) N = -N;
        float3 L = cb_SunDirection.xyz;                       // toward the sun
        float3 H = normalize(L + V);
        float  specTerm = pow(saturate(dot(N, H)), specPow);
        specular += specTerm * specInt * specShadow * saturate(dot(N, L)) * parallaxShadow;
    }

    // Emissive intensity scales the texture only (specular keeps its lit magnitude).
    // Applied pre-fog so distant emissives still get fog-dimmed.
    float3 surfaceColor = texColor.xyz * cb_MapParams.w * shadowScale + specular;

#if !NO_FOG
	float fogFactor = CalculateExponentialSquaredFog(In.ProjPos.w, cb_FogStart, cb_FogColor.w);
	fogFactor = saturate(fogFactor);
    // Apply fog by blending between fog color and original color
    float3 finalColor = lerp(cb_FogColor.xyz, surfaceColor, fogFactor);
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
        cb_Reflectivity.x,
        PackFresnelRoughness(cb_Reflectivity.y, surfRoughness));
    return Out;
}
