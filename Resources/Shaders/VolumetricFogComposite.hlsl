#include "ScreenSpace.hlsl"

Texture2D    fogTexture   : register( t0 );
Texture2D    sceneTexture : register( t1 );
Texture2D    depthTexture : register( t2 );
Texture2D    historyFogTexture : register( t3 );
SamplerState fogSampler   : register( s0 );
SamplerState sceneSampler : register( s1 );
SamplerState depthSampler : register( s2 );

cbuffer VolumetricFogCompositeCBuffer : register( b1 )
{
    float4 cb_DepthParams;      // unused, unused, near, far
    float4 cb_CompositeParams;  // x=temporal alpha, y=history valid, z=depth sharpness
};

float LinearizeDepth( float hwDepth )
{
    float invNear = 1.0f / max( cb_DepthParams.z, 0.00001f );
    float invFar  = 1.0f / max( cb_DepthParams.w, cb_DepthParams.z + 0.00001f );
    return 1.0f / ( hwDepth * ( invFar - invNear ) + invNear );
}

float3 SampleDepthAwareFog( float2 uv )
{
    uint width, height;
    fogTexture.GetDimensions( width, height );

    float2 fogSize = float2( (float)width, (float)height );
    float2 fogPos  = uv * fogSize - 0.5f;
    float2 basePos = floor( fogPos );
    float2 fracPos = fogPos - basePos;

    float centerDepth = LinearizeDepth( depthTexture.SampleLevel( depthSampler, uv, 0 ).r );

    float3 fogSum = 0.0f;
    float  weightSum = 0.0f;

    [unroll]
    for ( int y = 0; y < 2; y++ )
    {
        [unroll]
        for ( int x = 0; x < 2; x++ )
        {
            float2 tapPos = basePos + float2( (float)x, (float)y );
            float2 tapUV  = ( tapPos + 0.5f ) / fogSize;
            tapUV = saturate( tapUV );

            float2 bilinearWeights = lerp( 1.0f - fracPos, fracPos, float2( (float)x, (float)y ) );
            float bilinearWeight = bilinearWeights.x * bilinearWeights.y;

            float tapDepth = LinearizeDepth( depthTexture.SampleLevel( depthSampler, tapUV, 0 ).r );
            float depthWeight = exp( -abs( tapDepth - centerDepth ) * cb_CompositeParams.z / max( centerDepth, 1.0f ) );
            float weight = bilinearWeight * depthWeight;

            fogSum += fogTexture.SampleLevel( fogSampler, tapUV, 0 ).rgb * weight;
            weightSum += weight;
        }
    }

    return fogSum / max( weightSum, 0.0001f );
}

float4 ps_temporal( PS_IN i ) : SV_TARGET
{
    float3 current = fogTexture.SampleLevel( fogSampler, i.UV, 0 ).rgb;
    return float4( current, 1.0f );
}

float4 ps_additive( PS_IN i ) : SV_TARGET
{
    float3 fog = SampleDepthAwareFog( i.UV );
    float4 scene = sceneTexture.SampleLevel( sceneSampler, i.UV, 0 );
    return float4( scene.rgb + fog, scene.a );
}

float4 ps_darken( PS_IN i ) : SV_TARGET
{
    float darken = SampleDepthAwareFog( i.UV ).r;
    float4 scene = sceneTexture.SampleLevel( sceneSampler, i.UV, 0 );
    return float4( scene.rgb * darken, scene.a );
}
