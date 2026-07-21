// Cloud shadow bake. Renders a top-down "sun amount" map (1 = full sun, 0 = fully
// clouded) over a camera-centred world region. The shadow receivers sample it by
// worldPos.xz inside SampleShadow and multiply it into the sun term. The field is
// world-locked (sampled at true world coords) so it doesn't swim as the camera
// moves, and drifts over time via a wind offset.
//
// Noise/fbm adapted from "2D Clouds" by drift (Shadertoy) -- sky/colour removed.

#include "ScreenSpace.hlsl"

cbuffer CloudCBuffer : register(b1)
{
    float4 cb_Region; // xy = world region min (X,Z), z = world region size, w = feature scale
    float4 cb_Anim;   // x = time, y = wind dir x, z = wind dir y, w = coverage
    float4 cb_Shape;  // x = cloud alpha/density, y = contrast, zw = unused
};

static const float2x2 m = float2x2(1.6, 1.2, -1.2, 1.6);

float2 hash(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float noise(float2 p)
{
    const float K1 = 0.366025404;
    const float K2 = 0.211324865;
    float2 i = floor(p + (p.x + p.y) * K1);
    float2 a = p - i + (i.x + i.y) * K2;
    float2 o = (a.x > a.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    float2 b = a - o + K2;
    float2 c = a - 1.0 + 2.0 * K2;
    float3 h = max(0.5 - float3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    float3 n = h * h * h * h * float3(dot(a, hash(i + 0.0)), dot(b, hash(i + o)), dot(c, hash(i + 1.0)));
    return dot(n, float3(70.0, 70.0, 70.0));
}

float fbm(float2 n)
{
    float total = 0.0, amplitude = 0.1;
    [unroll] for (int i = 0; i < 3; i++)
    {
        total += noise(n) * amplitude;
        n = mul(m, n);
        amplitude *= 0.4;
    }
    return total;
}

float ps_main(PS_IN i) : SV_TARGET
{
    // Screen UV -> world XZ within the camera-centred region (world-locked sampling).
    float2 worldXZ = cb_Region.xy + i.UV * cb_Region.z;
    float2 uv      = worldXZ * cb_Region.w;        // feature scale -> noise space
    float2 wind    = cb_Anim.yz * cb_Anim.x;       // drift over time

    uv += wind;

    float q = fbm(uv * 0.5);

    // Ridged-noise shape.
    float r = 0.0;
    float f = 0.0;
    float2 nuv = uv - q;
    float rWeight = 0.8;
    float fWeight = 0.7;
    [unroll] for (int k = 0; k < 5; k++)
    {
        float n = noise(nuv);
        r += rWeight * abs(n);
        f += fWeight * n;
        nuv = mul(m, nuv);
        rWeight *= 0.7;
        fWeight *= 0.6;
    }
    f *= r + f;

    // Coverage threshold + density -> cloud opacity, then invert to sun amount.
    float density = saturate(cb_Anim.w + cb_Shape.x * f * r);
    density = saturate(density * cb_Shape.y); // contrast
    return 1.0 - density;
}
