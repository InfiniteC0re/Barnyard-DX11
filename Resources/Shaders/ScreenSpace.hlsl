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
