// STATIC: "ANIMATED" "0..1"

cbuffer ShadowPassBuffer : register(b0)
{
    float4x4 cb_matShadowMVP;
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

float4 vs_main_world(float3 pos : POSITION) : SV_POSITION
{
    return mul(float4(pos, 1.0f), cb_matShadowMVP);
}

float4 vs_main_skin(VS_IN_SKIN In, uint instanceID : SV_InstanceID) : SV_POSITION
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
    
    return mul(float4(vertex, 1.0), cb_matShadowMVP);
}
