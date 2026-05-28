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

float SampleShadow(float3 worldPos, float viewDepth)
{
    int cascade = 2;
    if (viewDepth < cb_CascadeSplits.x) cascade = 0;
    else if (viewDepth < cb_CascadeSplits.y) cascade = 1;

    if (cb_ShadowParams.z > 0.5f)
    {
        int forcedCascade = (int)cb_ShadowParams.z - 1;
        if (cb_ShadowFilterParams.z > 0.5f && cascade != forcedCascade)
            return 1.0f;

        cascade = forcedCascade;
    }

    float4 shadowPos = mul(float4(worldPos, 1.0f), cb_matLightVP[cascade]);
    shadowPos.xyz /= shadowPos.w;

    float2 shadowUV = shadowPos.xy * float2(0.5f, -0.5f) + 0.5f;
    float shadowZ = shadowPos.z - cb_ShadowParams.x;

    if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f) || shadowPos.z < 0.0f || shadowPos.z > 1.0f)
        return 1.0f;

    float shadow = 0.0f;
    float weightSum = 0.0f;
    float texelSize = cb_ShadowParams.y;
    int pcfRadius = (int)round(clamp(cb_ShadowFilterParams.x, 1.0f, 3.0f));
    float receiverPlaneBiasScale = cb_ShadowFilterParams.y;
    float2 depthBiasPerTexel = ComputeReceiverPlaneDepthBias(float3(shadowUV, shadowZ), texelSize) * receiverPlaneBiasScale;

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
                float3(shadowUV + sampleOffset * texelSize, cascade),
                shadowZ + dot(depthBiasPerTexel, sampleOffset)
            ) * weight;
            weightSum += weight;
        }
    }

    return shadow / weightSum;
}
