struct VS_IN
{
    float2 Position : POSITION;
    float2 UV : TEXCOORD;
};

struct PS_IN
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD;
};

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;
    Out.Position = float4(In.Position, 0.0f, 1.0f);
    Out.UV = In.UV;
    return Out;
}

float4 ps_red_tint(PS_IN In) : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 0.25f);
}

cbuffer BootBackgroundCB : register(b1)
{
    float4 g_vecBootParams; // x = time in seconds, y = aspect ratio (w/h), z = warm-up progress 0..1, w = surface height in pixels
};

// Source: https://www.shadertoy.com/view/fcf3Dn
#define BOOTBG_THRESHOLD 0.99f
#define BOOTBG_MIN_DIST 0.04f
#define BOOTBG_MAX_DIST 40.0f
#define BOOTBG_MAX_DRAWS 40
#define BOOTBG_AA 2
#define BOOTBG_PI 3.1415926535897932384626433832795f

float BootBG_Hash12(float2 p)
{
    uint2 q = uint2(int2(p)) * uint2(1597334673u, 3812015801u);
    uint n = (q.x ^ q.y) * 1597334673u;
    return float(n) * 2.328306437080797e-10f;
}

float BootBG_Value2D(float2 p)
{
    float2 pg = floor(p), pc = p - pg, k = float2(0.0f, 1.0f);
    pc *= pc * pc * (3.0f - 2.0f * pc);
    return lerp(
        lerp(BootBG_Hash12(pg + k.xx), BootBG_Hash12(pg + k.yx), pc.x),
        lerp(BootBG_Hash12(pg + k.xy), BootBG_Hash12(pg + k.yy), pc.x),
        pc.y);
}

float BootBG_GetStarsRough(float2 p)
{
    float s = smoothstep(BOOTBG_THRESHOLD, 1.0f, BootBG_Hash12(p));
    if (s >= BOOTBG_THRESHOLD)
        s = pow((s - BOOTBG_THRESHOLD) / (1.0f - BOOTBG_THRESHOLD), 10.0f);
    return s;
}

float BootBG_GetStars(float2 p, float a, float t)
{
    float2 pg = floor(p), pc = p - pg, k = float2(0.0f, 1.0f);
    pc *= pc * pc * (3.0f - 2.0f * pc);

    float s = lerp(
        lerp(BootBG_GetStarsRough(pg + k.xx), BootBG_GetStarsRough(pg + k.yx), pc.x),
        lerp(BootBG_GetStarsRough(pg + k.xy), BootBG_GetStarsRough(pg + k.yy), pc.x),
        pc.y);
    return smoothstep(a, a + t, s) * pow(BootBG_Value2D(p * 0.1f + g_vecBootParams.x) * 0.5f + 0.5f, 8.3f);
}

float BootBG_GetDust(float2 p, float2 size, float f)
{
    float2 ar = float2(g_vecBootParams.y, 1.0f);
    float2 pp = p * size * ar;
    float flTime = g_vecBootParams.x;
    return
        pow(0.64f + 0.46f * cos(p.x * 6.28f), 1.7f) * // keep stars at edges of the screen
        f *
        (
            BootBG_GetStars(0.1f * pp + flTime * float2(20.0f, -10.1f), 0.11f, 0.71f) * 4.0f +
            BootBG_GetStars(0.2f * pp + flTime * float2(30.0f, -10.1f), 0.1f, 0.31f) * 5.0f +
            BootBG_GetStars(0.32f * pp + flTime * float2(40.0f, -10.1f), 0.1f, 0.91f) * 2.0f
        );
}

float BootBG_SDF(float3 p)
{
    float flTime = g_vecBootParams.x;
    p *= 2.0f;

    float o = 8.2f * sin(0.05f * p.x + flTime * 0.25f) +
        (0.04f * p.z) *
        sin(p.x * 0.11f + flTime) *
        2.0f * sin(p.z * 0.2f + flTime) *
        BootBG_Value2D(float2(0.03f, 0.4f) * p.xz + float2(flTime * 0.5f, 0.0f));
    return abs(dot(p, normalize(float3(0.0f, 1.0f, 0.05f))) + 2.5f + o * 0.5f);
}

// x = accumulated glow, y = background dust visibility
float2 BootBG_Raymarch(float3 o, float3 d, float jitter)
{
    float t = jitter * 2.0f;
    float a = 0.0f;
    float g = BOOTBG_MAX_DIST;
    int dr = 0;

    [loop]
    for (int i = 0; i < 100; i++)
    {
        float3 p = o + d * t;

        float ndt = BootBG_SDF(p);

        g = (t > 10.0f) ? min(g, abs(ndt)) : BOOTBG_MAX_DIST;

        if (t >= BOOTBG_MAX_DIST)
            break;

        if (abs(ndt) < BOOTBG_MIN_DIST)
        {
            if (dr > BOOTBG_MAX_DRAWS)
                break;
            dr++;

            float f = smoothstep(0.0f, 0.3f, (p.z * 0.9f) / 100.0f);

            a += 0.015f * f;
            t += 0.05f;
        }
        else
        {
            t += abs(ndt) * 0.8f;
        }
    }

    g /= 3.0f;
    return float2(a, max(1.0f - g, 0.0f));
}

float BootBG_Dither(float2 pos)
{
    return frac(52.9829189f * frac(dot(pos, float2(0.06711056f, 0.00583715f))));
}

float3 BootBG_Render(float2 U, float2 ires)
{
    float2 uv = U / ires;

    float3 o = float3(0.0f, 0.0f, 0.0f);
    float3 d = float3((U - 0.5f * ires) / ires.y, 1.0f);

    float2 mg = BootBG_Raymarch(o, d, BootBG_Dither(U));
    float m = mg.x;

    // Grey at 0% and sweeping to the vivid blue as the warm-up progresses
    float p = 1.0f - 2.0f * g_vecBootParams.z;
    float3 l1 = lerp(float3(0.149f, 0.471f, 0.569f), float3(0.231f, 0.231f, 0.231f), p);
    float3 l2 = lerp(float3(0.075f, 0.333f, 0.412f), float3(0.129f, 0.129f, 0.129f), p);
    float3 l3 = lerp(float3(0.063f, 0.329f, 0.412f), float3(0.149f, 0.149f, 0.149f), p);
    float3 l4 = lerp(float3(0.169f, 0.482f, 0.580f), float3(0.251f, 0.251f, 0.251f), p);

    float3 c = lerp(
        lerp(l1, l2, uv.x),
        lerp(l3, l4, uv.x),
        uv.y);

    c = lerp(c, float3(1.0f, 1.0f, 1.0f), clamp(m, 0.0f, 1.0f));

    c += BootBG_GetDust(uv, float2(2000.0f, 2000.0f), mg.y) * 0.3f;

    return c;
}

float4 ps_boot_background(PS_IN In) : SV_TARGET
{
    float2 ires = float2(g_vecBootParams.w * g_vecBootParams.y, g_vecBootParams.w);

    // ShaderToy pixel coords are bottom-left origin
    float2 U = float2(In.UV.x, 1.0f - In.UV.y) * ires;

    float3 totalColor = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (int m = 0; m < BOOTBG_AA; m++)
    {
        [unroll]
        for (int n = 0; n < BOOTBG_AA; n++)
        {
            float2 offset = float2(float(m), float(n)) / float(BOOTBG_AA) - 0.5f;
            totalColor += BootBG_Render(U + offset, ires);
        }
    }
    totalColor /= float(BOOTBG_AA * BOOTBG_AA);

    totalColor += (BootBG_Dither(U) - 0.5f) / 255.0f;

    // sRGB-ish to linear so the tonemapped result matches the intended look
    return float4(pow(abs(totalColor), 2.2f), 1.0f);
}
