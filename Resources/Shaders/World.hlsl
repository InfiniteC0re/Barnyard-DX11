struct VS_IN
{
    float3 ObjPos : POSITION;
    float3 normal : NORMAL;
    float4 Color : Color;
    float2 UV : TEXCOORD0;
};

struct PS_IN
{
    float4 ProjPos : SV_POSITION;
    float4 Color : Color;
    float2 UV0 : TEXCOORD;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matMVP;
	float4   cb_TexCoordOffsetAndAlpha;
	float4   cb_AmbientColor;
	float4   cb_ShadowColor;
	float    cb_IsWater;
	float    cb_IsLit;
};

PS_IN vs_main(VS_IN In)
{
    PS_IN Out;

	// Calculate vertex screen position
    Out.ProjPos = mul(float4(In.ObjPos, 1.0f), cb_matMVP);
    
	// Calculate vertex Color based on current lighting settings and shadow factor (stored as vertex data)
	Out.Color.xyz = lerp(cb_ShadowColor.xyz, cb_AmbientColor.xyz, In.Color.xyz);
    
	// Adjust water Color
	// Water recieves less ambient Color
	Out.Color.xyz = (In.Color.xyz * cb_IsWater * 0.75f + Out.Color.xyz * 0.25f * cb_IsWater) + (Out.Color.xyz * cb_IsLit);

	// Calculate opacity
	// Used for water rings and probably something else
	Out.Color.w = (In.Color.x * cb_IsWater) + (1.0f * cb_IsLit);
	
	// Animate UV
    Out.UV0 = In.UV + cb_TexCoordOffsetAndAlpha.xy;

    return Out;
}

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 ps_main(PS_IN In) : SV_TARGET
{
    float4 tex_color = texture0.Sample(sampler0, In.UV0) * In.Color * cb_TexCoordOffsetAndAlpha.z;
	
#ifdef ALPHAREF
	// The only alpharef value used by the game is 128 (0.5f)
    if (tex_color.a < 0.5f) discard;
#endif

    return tex_color;
}
