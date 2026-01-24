struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD0;
};

struct PS_IN
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 cb_matMVP;
	float4 cb_TexCoordOffsetAndAlpha;
	float4 cb_AmbientColor;
	float4 cb_ShadowColor;
	float cb_IsWater;
	float cb_IsLit;
};

PS_IN vs_main(VS_IN input)
{
    PS_IN output;

	// Calculate vertex screen position
    output.position = mul(float4(input.position, 1.0f), cb_matMVP);
    
	// Calculate vertex color based on current lighting settings and shadow factor (stored as vertex data)
	output.color.xyz = lerp(cb_ShadowColor.xyz, cb_AmbientColor.xyz, input.color.xyz);
    
	// Adjust water color
	// Water recieves less ambient color
	output.color.xyz = (input.color.xyz * cb_IsWater * 0.75f + output.color.xyz * 0.25f * cb_IsWater) + (output.color.xyz * cb_IsLit);

	// Calculate opacity
	// Used for water rings and probably something else
	output.color.w = (input.color.x * cb_IsWater) + (1.0f * cb_IsLit);
	
	// Animate UV
    output.texcoord = input.texcoord + cb_TexCoordOffsetAndAlpha.xy;

    return output;
}

Texture2D albedo_texture : register(t0);
SamplerState albedo_texture_sampler : register(s0);

float4 ps_main(PS_IN input) : SV_TARGET
{
    float4 tex_color = albedo_texture.Sample(albedo_texture_sampler, input.texcoord) * input.color * cb_TexCoordOffsetAndAlpha.z;
	
#ifdef ALPHAREF
	// The only alpharef value used by the game is 128 (0.5f)
    if (tex_color.a < 0.5f) discard;
#endif

    return tex_color;
}
