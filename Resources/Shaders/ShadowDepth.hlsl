// STATIC: "ANIMATED" "0..1"
// STATIC: "ALPHATEST" "0..1"
// STATIC: "WIND" "0..1"

cbuffer ShadowPassBuffer : register(b0)
{
    float4x4 cb_matShadowMVP;
    float    cb_CurrentCascade;
    float3   cb_ShadowPad0;
    float4   cb_WindParams; // 5: xy = wind direction (world XZ), z = strength, w = time (phase)
    float4   cb_WindRemap;  // 6: x = windMin, y = windMax (remap the wind-strength channel into [0,1])
};

// Packed bone palette: 3 registers per bone, matching Skin.hlsl. Explicit column_major (shaders
// compile /Zpr): register j = column j of the bone transform, transposed on the CPU (48 bytes/bone)
cbuffer BoneCBuffer : register(b1)
{
    column_major float4x3 cb_bones[28];
};

// Declared up front (not just in the ALPHATEST block) so the skin wind path can sample in the VS
Texture2D    texture0 : register(t0); // alpha-test diffuse (PS)
SamplerState sampler0 : register(s0); // shared by the PS alpha test and the VS wind sample

#if WIND
// Skin meshes carry no vertex color, so wind strength is the roughness map's blue channel (mip 0);
// World meshes use the blue vertex-color channel instead
Texture2D windRoughnessMap : register(t8);

// Shared with World/Skin: remap strength through [windMin, windMax], offset along wind dir by two
// summed sines whose phase drifts with world XZ. Must match the main-pass deformation or shadows
// won't align
float3 ApplyWind(float3 a_pos, float a_strengthRaw)
{
    float mask  = saturate((a_strengthRaw - cb_WindRemap.x) / max(cb_WindRemap.y - cb_WindRemap.x, 1e-4f));
    float phase = cb_WindParams.w + dot(a_pos.xz, float2(0.35f, 0.35f));
    float sway  = sin(phase) + 0.5f * sin(phase * 2.7f + 1.3f);
    a_pos.xz   += cb_WindParams.xy * (sway * cb_WindParams.z * mask);
    return a_pos;
}
#endif

struct VS_IN_SKIN
{
    float3 ObjPos : POSITION;
    float3 Normal : NORMAL;
    float4 Weights : BLENDWEIGHT;
    float4 MIndices : BLENDINDICES;
    float2 UV : TEXCOORD0;
};

#ifdef ALPHATEST

struct VS_IN_WORLD
{
    float3 ObjPos : POSITION;
    float3 normal : NORMAL;
    float4 Color : Color;
    float2 UV : TEXCOORD0;
};

struct PS_IN
{
    float4 ProjPos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

#define VS_OUT PS_IN

#else  // ALPHATEST

#define VS_OUT float4

#endif // !ALPHATEST

VS_OUT vs_main_world(VS_IN_WORLD In)
{
    float3 objPos = In.ObjPos;
#if WIND
    objPos = ApplyWind(objPos, In.Color.z); // blue vertex-color channel = wind strength
#endif
    float4 proj = mul(float4(objPos, 1.0), cb_matShadowMVP);

#ifdef ALPHATEST

    VS_OUT result;
    result.ProjPos = proj;
    result.UV = In.UV;

    return result;

#else  // ALPHATEST

    return proj;

#endif // !ALPHATEST
}

VS_OUT vs_main_skin(VS_IN_SKIN In)
{
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
    for (int i = 0; i < 4; ++i)
    {
        float4x3 BoneMatrix = cb_bones[BoneIndices[i]];
        vertex += mul(float4(In.ObjPos, 1.0), BoneMatrix) * BoneWeights[i];
    }
	
#else // ANIMATED
	
	float3 vertex = In.ObjPos;

#endif // !ANIMATED

#if WIND
    float windBlue = windRoughnessMap.SampleLevel(sampler0, In.UV, 0).b; // roughness blue = strength
    vertex = ApplyWind(vertex, windBlue);
#endif

    float4 proj = mul(float4(vertex, 1.0), cb_matShadowMVP);

#ifdef ALPHATEST

    VS_OUT result;
    result.ProjPos = proj;
    result.UV = In.UV;

    return result;

#else  // ALPHATEST

    return proj;

#endif // !ALPHATEST
}

#ifdef ALPHATEST

float4 ps_main(VS_OUT In) : SV_TARGET
{
    float4 texColor = texture0.Sample(sampler0, In.UV);
	clip(texColor.a - (0.8f - cb_CurrentCascade * 0.35f));

    return float4(1, 1, 1, 1);
}

#endif // ALPHATEST
