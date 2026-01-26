struct VS_IN
{
	float3 ObjPos	: POSITION;		// Object space position
	float3 Normal	: NORMAL;		// Vertex normal
	float4 Weights	: BLENDWEIGHT;	// Weights
	float4 MIndices	: BLENDINDICES;	// Matrix Indices
	float2 UV		: TEXCOORD;		// UV
};

struct PS_IN
{
	float4 ProjPos	: SV_POSITION;	// Projected space position 
	// float3 Normal	: NORMAL;
	// float4 Color	: COLOR;
	float2 UV0		: TEXCOORD0;	// UV
	// float4 ViewPos	: TEXCOORD3;	// View space position
	// float FogFactor	: FOG;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matWVP;
    float4x4 cb_bones[28];
};

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

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
	float3 normal = 0;
	for (int i = 0; i < 4; ++i)
	{
		float4x3 BoneMatrix = cb_bones[BoneIndices[i]];
		vertex += mul(float4(In.ObjPos, 1.0), BoneMatrix) * BoneWeights[i];
	}

	Out.ProjPos = mul(float4(vertex, 1.0), cb_matWVP);
    Out.UV0 = In.UV;

    return Out;
}

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 ps_main(PS_IN In) : SV_TARGET
{
    return texture0.Sample(sampler0, In.UV0);
}
