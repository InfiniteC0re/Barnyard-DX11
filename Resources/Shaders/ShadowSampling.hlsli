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
};

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

ShadowSetup ComputeShadowSetup(float3 worldPos, int cascade)
{
    float4 shadowPos = mul(float4(worldPos, 1.0f), cb_matLightVP[cascade]);
    shadowPos.xyz /= shadowPos.w;

    ShadowSetup setup;
    setup.shadowUV = shadowPos.xy * float2(0.5f, -0.5f) + 0.5f;
    setup.shadowZ = shadowPos.z - cb_ShadowParams.x;
    setup.outside = any(setup.shadowUV < 0.0f) || any(setup.shadowUV > 1.0f) || shadowPos.z < 0.0f || shadowPos.z > 1.0f;

    float texelSize = cb_ShadowParams.y;
    float receiverPlaneBiasScale = cb_ShadowFilterParams.y;
    setup.depthBiasPerTexel = ComputeReceiverPlaneDepthBias(float3(setup.shadowUV, setup.shadowZ), texelSize) * receiverPlaneBiasScale;
    return setup;
}

float SampleShadowPCF(ShadowSetup setup, int cascade)
{
    if (setup.outside)
        return 1.0f;

    float shadow = 0.0f;
    float weightSum = 0.0f;
    float texelSize = cb_ShadowParams.y;
    int pcfRadius = (int)round(clamp(cb_ShadowFilterParams.x, 1.0f, 3.0f));

    [unroll] for (int dy = -3; dy <= 3; dy++)
    {
        [unroll] for (int dx = -3; dx <= 3; dx++)
        {
            if (abs(dx) > pcfRadius || abs(dy) > pcfRadius)
                continue;

            float2 sampleOffset = float2(dx, dy);
            float weight = (pcfRadius + 1.0f - abs((float)dx)) * (pcfRadius + 1.0f - abs((float)dy));
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

float SampleShadowCascade(float3 worldPos, int cascade)
{
    ShadowSetup setup = ComputeShadowSetup(worldPos, cascade);
    return SampleShadowPCF(setup, cascade);
}

float SampleShadow(float3 worldPos, float viewDepth)
{
    int cascade = 2;
    if (viewDepth < cb_CascadeSplits.x) cascade = 0;
    else if (viewDepth < cb_CascadeSplits.y) cascade = 1;

    // Debug: force a single cascade and (optionally) mask everything outside it.
    if (cb_ShadowParams.z > 0.5f)
    {
        int forcedCascade = (int)cb_ShadowParams.z - 1;
        if (cb_ShadowFilterParams.z > 0.5f && cascade != forcedCascade)
            return 1.0f;

        return SampleShadowCascade(worldPos, forcedCascade);
    }

    // Compute gradient-dependent setup for both the current and next cascade
    // unconditionally (uniform control flow, required for ddx/ddy). The PCF
    // loops below carry no gradients, so they can be skipped per-pixel.
    int nextCascade = min(cascade + 1, 2);
    ShadowSetup setup     = ComputeShadowSetup(worldPos, cascade);
    ShadowSetup nextSetup = ComputeShadowSetup(worldPos, nextCascade);

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

    return shadow;
}
