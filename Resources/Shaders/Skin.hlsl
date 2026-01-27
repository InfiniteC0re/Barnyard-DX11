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
	float2 UV1		: TEXCOORD1;	// UV for lighting
	// float4 ViewPos	: TEXCOORD3;	// View space position
	// float FogFactor	: FOG;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matWVP;
	float4	 cb_ambientColor;
	float4	 cb_lightDirection;
	float4	 cb_upAxis;
	float4	 cb_lightingLerp1;
	float4	 cb_lightingLerp2;
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
		float4x3 BoneMatrix = (float4x3)cb_bones[BoneIndices[i]];
		vertex += mul(float4(In.ObjPos, 1.0), BoneMatrix) * BoneWeights[i];

		float3x3 BoneNormal = (float3x3)BoneMatrix;
		normal += mul(In.Normal, BoneNormal) * BoneWeights[i];
	}

	normal = normalize(normal);
	Out.ProjPos = mul(float4(vertex, 1.0), cb_matWVP);
    Out.UV0 = In.UV;

	// Lighting UV
    Out.UV1.x = dot(normal, -cb_lightDirection.xyz);
    Out.UV1.y = dot(normal, cb_upAxis.xyz);

    return Out;
}

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

Texture2D lighting1 : register(t1);
Texture2D lighting2 : register(t2);
Texture2D lighting3 : register(t3);
Texture2D lighting4 : register(t4);
SamplerState samplerLighting : register(s1);

float4 ps_main(PS_IN In) : SV_TARGET
{
    float4 texColor = texture0.Sample(sampler0, In.UV0);

	float3 lighting1Color = lighting1.Sample(samplerLighting, In.UV1);
	float3 lighting2Color = lighting2.Sample(samplerLighting, In.UV1);
	float3 lighting3Color = lighting3.Sample(samplerLighting, In.UV1);
	float3 lighting4Color = lighting4.Sample(samplerLighting, In.UV1);

	texColor.rgb = clamp(texColor.rgb - lerp(lighting3Color, lighting1Color, cb_lightingLerp1).rgb, 0.0f, 1.0f);
	texColor.rgb = clamp(texColor.rgb + lerp(lighting2Color, lighting4Color, cb_lightingLerp2).rgb, 0.0f, 1.0f);
	texColor.a *= cb_ambientColor.a;

	return texColor;
}
