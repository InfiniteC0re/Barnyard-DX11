#ifndef SHADOW_TEXTURE_REGISTER
#define SHADOW_TEXTURE_REGISTER t2
#endif

#ifndef SHADOW_SAMPLER_REGISTER
#define SHADOW_SAMPLER_REGISTER s2
#endif

cbuffer ShadowBuffer : register(b1)
{
    float4x4 cb_matLightVP[3];
    float4   cb_CascadeSplits;
    float4   cb_ShadowParams;
    float4   cb_ShadowFilterParams;
    float4   cb_CascadeScales;        // x,y,z = per-cascade atlas UV scale (renderRes / atlasRes); w = normal-offset scale (texels)
    float4   cb_CascadeReceiverBias;  // x,y,z = per-cascade receiver depth bias
    float4   cb_CascadePCFRadius;     // x,y,z = per-cascade PCF kernel radius
    float4   cb_CascadeWorldTexelSize;// x,y,z = per-cascade world units per shadow texel
    float4   cb_LightDirection;       // xyz = sun travel direction (sun->scene); w = grazing offset cap
    float4   cb_CloudParams;          // xy = world region min (X,Z), z = 1/region size, w = strength (0 = off)
};

#if CLOUD_SHADOWS
#ifndef CLOUD_TEXTURE_REGISTER
#define CLOUD_TEXTURE_REGISTER t9
#endif
#ifndef CLOUD_SAMPLER_REGISTER
#define CLOUD_SAMPLER_REGISTER s3
#endif

Texture2D            cloudShadowTex     : register(CLOUD_TEXTURE_REGISTER);
SamplerState         cloudShadowSampler : register(CLOUD_SAMPLER_REGISTER);

// Animated cloud shadow: sample the baked top-down sun-amount map by world XZ and
// scale by strength. Outside the camera-centred region => full sun (1).
float SampleCloudLight(float2 worldXZ)
{
    if (cb_CloudParams.w <= 0.0f)
        return 1.0f;

    float2 uv = (worldXZ - cb_CloudParams.xy) * cb_CloudParams.z;
    if (any(uv < 0.0f) || any(uv > 1.0f))
        return 1.0f;

    // Fade the cloud contribution out over the outer ~15% of the region so the edge
    // of the camera-centred bake doesn't show a hard line as it slides with the camera.
    float2 d    = min(uv, 1.0f - uv);
    float  edge = smoothstep(0.0f, 0.15f, min(d.x, d.y));

    float sun = cloudShadowTex.SampleLevel(cloudShadowSampler, uv, 0).r;
    return lerp(1.0f, sun, edge * cb_CloudParams.w);
}
#endif // CLOUD_SHADOWS

Texture2DArray         shadowMaps    : register(SHADOW_TEXTURE_REGISTER);
SamplerComparisonState shadowSampler : register(SHADOW_SAMPLER_REGISTER);

float2 ComputeReceiverPlaneDepthBias(float3 shadowCoord, float texelSize)
{
    float3 shadowDDX = ddx(shadowCoord);
    float3 shadowDDY = ddy(shadowCoord);

    float determinant = shadowDDX.x * shadowDDY.y - shadowDDX.y * shadowDDY.x;
    if (abs(determinant) < 0.000001f)
        return 0.0f;

    float invDeterminant = 1.0f / determinant;
    float2x2 shadowToScreen = float2x2(
        shadowDDY.y * invDeterminant, -shadowDDX.y * invDeterminant,
        -shadowDDY.x * invDeterminant, shadowDDX.x * invDeterminant
    );

    float2 rightDepthRatio = mul(float2(texelSize, 0.0f), shadowToScreen);
    float2 upDepthRatio = mul(float2(0.0f, texelSize), shadowToScreen);
    float2 depthDerivatives = float2(shadowDDX.z, shadowDDY.z);

    return float2(
        dot(rightDepthRatio, depthDerivatives),
        dot(upDepthRatio, depthDerivatives)
    );
}

// Gradient-dependent setup for a cascade. Must be called under uniform control
// flow because ComputeReceiverPlaneDepthBias uses ddx/ddy. Outputs are consumed
// by SampleShadowPCF, which carries no gradients and may run conditionally.
struct ShadowSetup
{
    float2 shadowUV;
    float  shadowZ;
    float2 depthBiasPerTexel;
    bool   outside;
};

ShadowSetup ComputeShadowSetup(float3 worldPos, float3 worldNormal, int cascade)
{
    // Normal-offset bias: push the receiver along its surface normal before
    // projecting. The offset distance MUST be the same for every cascade -- scaling
    // by each cascade's own (very different) world texel size pushed the sample by
    // different amounts per cascade, so the shadow landed in a different place in each
    // one and produced a visible seam / position jump at cascade boundaries. Use
    // cascade 0's texel size as the single reference for all cascades.
    //
    // Grazing scale: a texel projects into a long streak on surfaces near edge-on to
    // the light, so the error grows as the surface faces away from the sun. Scale the
    // offset by 1/NdotL (capped) so grazing walls get more push than head-on surfaces.
    // It depends only on the per-pixel normal, so it stays consistent across cascades.
    float NdotL        = saturate(dot(worldNormal, -cb_LightDirection.xyz));
    float grazingScale = clamp(1.0f / max(NdotL, 0.05f), 1.0f, cb_LightDirection.w);
    worldPos += worldNormal * (cb_CascadeWorldTexelSize.x * cb_CascadeScales.w * grazingScale);

    float4 shadowPos = mul(float4(worldPos, 1.0f), cb_matLightVP[cascade]);
    shadowPos.xyz /= shadowPos.w;

    // [0,1] coords within the cascade frustum; scale into the cascade's atlas sub-rect.
    float2 cascadeUV = shadowPos.xy * float2(0.5f, -0.5f) + 0.5f;

    ShadowSetup setup;
    setup.shadowUV = cascadeUV * cb_CascadeScales[cascade];
    setup.shadowZ = shadowPos.z - cb_CascadeReceiverBias[cascade];
    setup.outside = any(cascadeUV < 0.0f) || any(cascadeUV > 1.0f) || shadowPos.z < 0.0f || shadowPos.z > 1.0f;

    float texelSize = cb_ShadowParams.y;
    float receiverPlaneBiasScale = cb_ShadowFilterParams.y;
    setup.depthBiasPerTexel = ComputeReceiverPlaneDepthBias(float3(setup.shadowUV, setup.shadowZ), texelSize) * receiverPlaneBiasScale;
    return setup;
}

// Compile-time PCF kernel half-size. MUST be a constant: a runtime radius forces the
// compiler to unroll the full 7x7 with a per-tap branch (~49 conditional texture
// samples per call, emitted again for the cascade-blend and debug paths -> a ~1800
// instruction shader). A constant collapses it to a tight (2R+1)^2 unconditional loop.
// 1 = 3x3 (9 taps). Raise to 2 for softer 5x5 (25 taps) shadows at a fixed cost.
#ifndef SHADOW_PCF_RADIUS
#define SHADOW_PCF_RADIUS 1
#endif

float SampleShadowPCF(ShadowSetup setup, int cascade)
{
    if (setup.outside)
        return 1.0f;

    float shadow = 0.0f;
    float weightSum = 0.0f;
    float texelSize = cb_ShadowParams.y;

    [unroll] for (int dy = -SHADOW_PCF_RADIUS; dy <= SHADOW_PCF_RADIUS; dy++)
    {
        [unroll] for (int dx = -SHADOW_PCF_RADIUS; dx <= SHADOW_PCF_RADIUS; dx++)
        {
            float2 sampleOffset = float2(dx, dy);
            float  weight = (SHADOW_PCF_RADIUS + 1.0f - abs((float)dx)) * (SHADOW_PCF_RADIUS + 1.0f - abs((float)dy));
            shadow += shadowMaps.SampleCmpLevelZero(
                shadowSampler,
                float3(setup.shadowUV + sampleOffset * texelSize, cascade),
                setup.shadowZ + dot(setup.depthBiasPerTexel, sampleOffset)
            ) * weight;
            weightSum += weight;
        }
    }

    return shadow / weightSum;
}

float SampleShadowCascade(float3 worldPos, float3 worldNormal, int cascade)
{
    ShadowSetup setup = ComputeShadowSetup(worldPos, worldNormal, cascade);
    return SampleShadowPCF(setup, cascade);
}

float SampleShadow(float3 worldPos, float3 worldNormalRaw, float viewDepth)
{
    // Some meshes ship with zero/degenerate normals (which become NaN after the
    // vertex-shader normalize). Guard here so the normal-offset bias falls back to
    // "no offset" instead of corrupting the lookup.
    float normalLen = length(worldNormalRaw);
    float3 worldNormal = normalLen > 1e-4f ? (worldNormalRaw / normalLen) : float3(0.0f, 0.0f, 0.0f);

    int cascade = 2;
    if (viewDepth < cb_CascadeSplits.x) cascade = 0;
    else if (viewDepth < cb_CascadeSplits.y) cascade = 1;

    // Debug: force a single cascade and (optionally) mask everything outside it.
    if (cb_ShadowParams.z > 0.5f)
    {
        int forcedCascade = (int)cb_ShadowParams.z - 1;
        if (cb_ShadowFilterParams.z > 0.5f && cascade != forcedCascade)
            return 1.0f;

        return SampleShadowCascade(worldPos, worldNormal, forcedCascade);
    }

    // Compute gradient-dependent setup for both the current and next cascade
    // unconditionally (uniform control flow, required for ddx/ddy). The PCF
    // loops below carry no gradients, so they can be skipped per-pixel.
    int nextCascade = min(cascade + 1, 2);
    ShadowSetup setup     = ComputeShadowSetup(worldPos, worldNormal, cascade);
    ShadowSetup nextSetup = ComputeShadowSetup(worldPos, worldNormal, nextCascade);

    float shadow = SampleShadowPCF(setup, cascade);

    // Smooth fade across the cascade boundary: within the last fraction of a
    // cascade's depth range, cross-fade into the next (coarser) cascade so the
    // transition seam disappears. The expensive second PCF only runs for pixels
    // that are actually inside the blend band.
    float blendFraction = cb_ShadowFilterParams.w;
    float splitDist = (cascade == 0) ? cb_CascadeSplits.x : cb_CascadeSplits.y;
    float prevSplit = (cascade == 0) ? 0.0f : cb_CascadeSplits.x;
    float blendBand = max((splitDist - prevSplit) * blendFraction, 1e-4f);
    float fadeStart = splitDist - blendBand;

    if (blendFraction > 0.0f && cascade < 2 && viewDepth > fadeStart)
    {
        float t = saturate((viewDepth - fadeStart) / blendBand);
        float nextShadow = SampleShadowPCF(nextSetup, nextCascade);
        shadow = lerp(shadow, nextShadow, t);
    }

    // Animated cloud shadows modulate the sun term the same way the geometric shadow
    // does (world XZ -> baked sun-amount map). Compiled out by the CLOUD_SHADOWS combo.
#if CLOUD_SHADOWS
    shadow *= SampleCloudLight(worldPos.xz);
#endif
    return shadow;
}
