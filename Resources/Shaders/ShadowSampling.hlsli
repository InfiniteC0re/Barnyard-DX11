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

float2 ComputeReceiverPlaneDepthBias(float3 shadowDDX, float3 shadowDDY, float texelSize)
{
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

struct ShadowReceiver
{
    float3 offsetPos; // normal-offset receiver position (world)
    float3 posDDX;    // ddx(offsetPos)
    float3 posDDY;    // ddy(offsetPos)
};

ShadowReceiver ComputeShadowReceiver(float3 worldPos, float3 worldNormal)
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
    // Toward-sun via the engine's (-x, +y, -z) mapping of the sun-travel vector (see the volumetric
    // fog comment in ERRenderWrapper). Full negation had the wrong Y sign, misfiring the grazing test
    float3 toSun       = float3(-cb_LightDirection.x, cb_LightDirection.y, -cb_LightDirection.z);
    float NdotL        = saturate(dot(worldNormal, toSun));
    float grazingScale = clamp(1.0f / max(NdotL, 0.05f), 1.0f, cb_LightDirection.w);

    ShadowReceiver receiver;
    receiver.offsetPos = worldPos + worldNormal * (cb_CascadeWorldTexelSize.x * cb_CascadeScales.w * grazingScale);
    receiver.posDDX    = ddx(receiver.offsetPos);
    receiver.posDDY    = ddy(receiver.offsetPos);
    return receiver;
}

struct ShadowSetup
{
    float2 shadowUV;
    float  shadowZ;
    float2 depthBiasPerTexel;
    bool   outside;
};

ShadowSetup ComputeShadowSetup(ShadowReceiver a_receiver, int cascade)
{
    float4 shadowPos = mul(float4(a_receiver.offsetPos, 1.0f), cb_matLightVP[cascade]);
    shadowPos.xyz /= shadowPos.w;

    // [0,1] coords within the cascade frustum; scale into the cascade's atlas sub-rect.
    float2 cascadeUV = shadowPos.xy * float2(0.5f, -0.5f) + 0.5f;

    ShadowSetup setup;
    setup.shadowUV = cascadeUV * cb_CascadeScales[cascade];
    setup.shadowZ = shadowPos.z - cb_CascadeReceiverBias[cascade];
    setup.outside = any(cascadeUV < 0.0f) || any(cascadeUV > 1.0f) || shadowPos.z < 0.0f || shadowPos.z > 1.0f;

    float  uvScale  = cb_CascadeScales[cascade];
    float3 projDDX  = mul(float4(a_receiver.posDDX, 0.0f), cb_matLightVP[cascade]).xyz;
    float3 projDDY  = mul(float4(a_receiver.posDDY, 0.0f), cb_matLightVP[cascade]).xyz;
    float3 coordDDX = float3(projDDX.xy * float2(0.5f, -0.5f) * uvScale, projDDX.z);
    float3 coordDDY = float3(projDDY.xy * float2(0.5f, -0.5f) * uvScale, projDDY.z);

    float texelSize = cb_ShadowParams.y;
    float receiverPlaneBiasScale = cb_ShadowFilterParams.y;
    setup.depthBiasPerTexel = ComputeReceiverPlaneDepthBias(coordDDX, coordDDY, texelSize) * receiverPlaneBiasScale;
    return setup;
}

// Per-cascade compile-time PCF kernel half-size. These MUST be compile-time constants: a
// runtime radius forces the compiler to unroll the full 7x7 with a per-tap branch (~49
// conditional samples per call, re-emitted for the cascade-blend and debug paths). Distant
// geometry is tiny on screen, so the far cascade uses a smaller kernel than the near one. The
// comparison sampler is LINEAR, so even radius 0 is a hardware 2x2 PCF -- the far cascade stays
// smooth, just with a smaller footprint. 0 = single (bilinear) tap, 1 = 3x3, 2 = 5x5.
#ifndef SHADOW_PCF_RADIUS
#define SHADOW_PCF_RADIUS 1                    // near/mid cascades: full kernel
#endif
#ifndef SHADOW_PCF_RADIUS_C2
#define SHADOW_PCF_RADIUS_C2 0                 // far cascade: single bilinear tap
#endif

// Fixed-radius PCF kernel. RADIUS is a compile-time literal at every call site (see
// SampleShadowPCF below), so fxc constant-folds the loop bounds and unrolls each instance into
// a tight (2R+1)^2 loop -- there is no runtime loop bound.
float SampleShadowKernel(ShadowSetup setup, int cascade, int RADIUS)
{
    float shadow = 0.0f;
    float weightSum = 0.0f;
    float texelSize = cb_ShadowParams.y;

    [unroll] for (int dy = -RADIUS; dy <= RADIUS; dy++)
    {
        [unroll] for (int dx = -RADIUS; dx <= RADIUS; dx++)
        {
            float2 sampleOffset = float2(dx, dy);
            float  weight = (RADIUS + 1.0f - abs((float)dx)) * (RADIUS + 1.0f - abs((float)dy));
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

// Dispatch to the cascade's kernel. The cascade index is dynamic, but each branch passes a
// compile-time literal radius, so every kernel instance is a fixed-size unroll (the far
// cascade gets a cheaper kernel than the near one).
float SampleShadowPCF(ShadowSetup setup, int cascade)
{
    if (setup.outside)
        return 1.0f;

    // One shared kernel for cascades 0/1 so a wave straddling their split runs a single
    // unrolled kernel instead of two identical copies
    if (cascade < 2) return SampleShadowKernel(setup, cascade, SHADOW_PCF_RADIUS);
    return SampleShadowKernel(setup, cascade, SHADOW_PCF_RADIUS_C2);
}

float SampleShadowCascade(float3 worldPos, float3 worldNormal, int cascade)
{
    ShadowReceiver receiver = ComputeShadowReceiver(worldPos, worldNormal);
    ShadowSetup    setup    = ComputeShadowSetup(receiver, cascade);
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

    ShadowReceiver receiver = ComputeShadowReceiver(worldPos, worldNormal);
    ShadowSetup    setup    = ComputeShadowSetup(receiver, cascade);

    float shadow = SampleShadowPCF(setup, cascade);

    // Smooth fade across the cascade boundary: within the last fraction of a
    // cascade's depth range, cross-fade into the next (coarser) cascade so the
    // transition seam disappears. The expensive second setup + PCF only run for
    // pixels that are actually inside the blend band.
    float blendFraction = cb_ShadowFilterParams.w;
    float splitDist = (cascade == 0) ? cb_CascadeSplits.x : cb_CascadeSplits.y;
    float prevSplit = (cascade == 0) ? 0.0f : cb_CascadeSplits.x;
    float blendBand = max((splitDist - prevSplit) * blendFraction, 1e-4f);
    float fadeStart = splitDist - blendBand;

    if (blendFraction > 0.0f && cascade < 2 && viewDepth > fadeStart)
    {
        int   nextCascade = min(cascade + 1, 2);
        float t = saturate((viewDepth - fadeStart) / blendBand);
        ShadowSetup nextSetup  = ComputeShadowSetup(receiver, nextCascade);
        float       nextShadow = SampleShadowPCF(nextSetup, nextCascade);
        shadow = lerp(shadow, nextShadow, t);
    }

    // Animated cloud shadows modulate the sun term the same way the geometric shadow
    // does (world XZ -> baked sun-amount map). Compiled out by the CLOUD_SHADOWS combo.
#if CLOUD_SHADOWS
    shadow *= SampleCloudLight(worldPos.xz);
#endif
    return shadow;
}
