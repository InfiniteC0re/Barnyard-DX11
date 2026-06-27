// STATIC: "ANIMATED" "0..1"
// STATIC: "ALPHATEST" "0..1"

cbuffer ShadowPassBuffer : register(b0)
{
    float4x4 cb_matShadowMVP;
    float cb_CurrentCascade;
};

cbuffer BoneCBuffer : register(b1)
{
    float4x4 cb_bones[28];
};

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
    float4 proj = mul(float4(In.ObjPos, 1.0), cb_matShadowMVP);
    
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
        float4x3 BoneMatrix = (float4x3)cb_bones[BoneIndices[i]];
        vertex += mul(float4(In.ObjPos, 1.0), BoneMatrix) * BoneWeights[i];
    }
	
#else // ANIMATED
	
	float3 vertex = In.ObjPos;

#endif // !ANIMATED

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

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 ps_main(VS_OUT In) : SV_TARGET
{
    float4 texColor = texture0.Sample(sampler0, In.UV);
	clip(texColor.a - (0.8f - cb_CurrentCascade * 0.35f));

    return float4(1, 1, 1, 1);
}

#endif // ALPHATEST
