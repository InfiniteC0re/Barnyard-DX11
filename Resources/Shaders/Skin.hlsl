struct VS_IN
{
	float3 ObjPos	: POSITION;		// Object space position
    float3 Normal	: NORMAL;		// Vertex normal
	float4 Weights	: BLENDWEIGHT;	// Weights
	float4 MIndices	: BLENDINDICES;	// Matrix Indices
	float2 UV		: TEXCOORD0;	// UV
};

struct PS_IN
{
	float4 ProjPos	: SV_POSITION;		// Projected space position 
	// float3 Normal	: NORMAL;
	float2 UV0		: TEXCOORD0;		// UV
	// float4 ViewPos	: TEXCOORD3;	// View space position
	// float FogFactor	: FOG;
#ifdef BAKED_LIGHTING
	float2 UV1		: TEXCOORD1;		// UV for baked lighting
#else // BAKED_LIGHTING
	float4 Color	: COLOR;
#endif // !BAKED_LIGHTING
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matWVP;
	float4	 cb_ambientColor;
	float4	 cb_lightColor;		// xyz = light color, w = alpha ref
	float4	 cb_lightDirection;
	float4	 cb_upAxis;
	float4	 cb_lightingLerp1;
	float4	 cb_lightingLerp2;
	float    cb_FogStart;
	float    cb_FogEnd;
	float4   cb_FogColor;
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

	Out.ProjPos = mul(float4(vertex, 1.0), cb_matWVP);
    Out.UV0 = In.UV;

	// Lighting UV
	float NdotL = dot(normal, -cb_lightDirection.xyz);

#ifdef BAKED_LIGHTING

	// Baked Characters Lighting
	NdotL = clamp(NdotL, 0.0f, 1.0f);

    Out.UV1.x = NdotL;
    Out.UV1.y = dot(normal, cb_upAxis.xyz);

#elif FOB // BAKED_LIGHTING

	// FOB Lighting
	// NdotL = abs(NdotL);
	NdotL = clamp(NdotL, 0.0f, 1.0f);

	// float3 lightColor = cb_ambientColor.xyz;
	// lightColor.r *= 0.9f;
	// lightColor.z *= 0.5f;

	// lightColor += NdotL * 1.2f * cb_lightColor.xyz;

	// Out.Color.xyz = lightColor;

	const float3 baseColor = float3(0.54509807f, 0.60784316f, 0.47058824f);
	const float3 lightColor = float3(0.7372549f, 0.8156863f, 0.5254902f);
	// float3(0.54509807f, 0.60784316f, 0.47058824f) - usual
	// float3(0.9529412f, 0.75686276f, 0.54509807f) - yellow
	// float3(0.7372549f, 0.8156863f, 0.5254902f) - lighted

	float3 colorA = float3(188, 201, 103) / 255.0f;
	float3 colorB = float3(111, 114, 143) / 255.0f;

	// Out.Color.xyz = NdotL * cb_lightColor.xyz + cb_ambientColor.xyz;
	// Out.Color.xyz = float3(1.0f, 1.0f, 1.0f) * (NdotL * lightColor + (1.0f - NdotL) * baseColor);
	Out.Color.xyz = colorB + (colorA - colorB) * 1;
	// Out.Color.xyz = lerp(float3(0.54509807f, 0.60784316f, 0.47058824f), cb_lightColor.xyz + float3(0.7372549f, 0.8156863f, 0.5254902f), NdotL);

#else // !FOB && !BAKED_LIGHTING

	// Runtime lighting calculation

	NdotL = clamp(NdotL, 0.0f, 1.0f);

	// Original PC shading:
	// Out.Color.xyz = NdotL * cb_lightColor.xyz + (1.0f - NdotL) * cb_ambientColor.xyz;

	// Correct console shading:
	Out.Color.xyz = NdotL * cb_lightColor.xyz + cb_ambientColor.xyz;
	
	Out.Color.w = cb_ambientColor.a;

#endif // !BAKED_LIGHTING

    return Out;
}

float CalculateExponentialFog(float distance, float fogStart, float density)
{
    if (distance <= fogStart) return 1.0f;
    return exp(-density * distance);
}

float CalculateExponentialSquaredFog(float distance, float fogStart, float density)
{
    if (distance <= fogStart) return 1.0f;
    return exp(-pow(density * (distance - fogStart), 2));
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
	clip(texColor.a - cb_lightColor.w);

#ifdef BAKED_LIGHTING
	float3 lighting1Color = lighting1.Sample(samplerLighting, In.UV1);
	float3 lighting2Color = lighting2.Sample(samplerLighting, In.UV1);
	float3 lighting3Color = lighting3.Sample(samplerLighting, In.UV1);
	float3 lighting4Color = lighting4.Sample(samplerLighting, In.UV1);

	texColor.rgb = clamp(texColor.rgb - lerp(lighting3Color, lighting1Color, cb_lightingLerp1).rgb, 0.0f, 1.0f);
	texColor.rgb = clamp(texColor.rgb + lerp(lighting2Color, lighting4Color, cb_lightingLerp2).rgb, 0.0f, 1.0f);
	texColor.a *= cb_ambientColor.a;
#else // BAKED_LIGHTING
	texColor.rgb = texColor.rgb * In.Color;
	texColor.a *= cb_ambientColor.a;
#endif // !BAKED_LIGHTING

	texColor.rgb = saturate(texColor.rgb);

	float fogFactor = CalculateExponentialSquaredFog(In.ProjPos.w, cb_FogStart, cb_FogColor.w);
	fogFactor = saturate(fogFactor);

	// Apply fog by blending between fog color and original color
    float3 finalColor = lerp(cb_FogColor.xyz, texColor.xyz, fogFactor);
    
    return float4(finalColor, texColor.a);
}
