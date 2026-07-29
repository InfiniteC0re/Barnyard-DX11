#include "ScreenSpace.hlsl"

Texture2D    aoTexture    : register( t0 );
Texture2D    depthTexture : register( t1 );
SamplerState pointSampler : register( s0 );
SamplerState linearSampler : register( s1 );

cbuffer HBAOBlurCBuffer : register( b1 )
{
    float4 cb_BlurParams; // invWidth, invHeight, directionX, directionY
    float4 cb_DepthParams; // near, far, sharpness, unused
};

float LinearizeDepth( float hardwareDepth )
{
    float invNear = 1.0f / max( cb_DepthParams.x, 0.00001f );
    float invFar = 1.0f / max( cb_DepthParams.y, cb_DepthParams.x + 0.00001f );
    return 1.0f / ( hardwareDepth * ( invFar - invNear ) + invNear );
}

float ps_main( PS_IN i ) : SV_TARGET
{
    // Sky is written as 1.0 by the AO pass; skipping it here saves the whole 20-tap kernel over
    // however much of an outdoor level is sky
    float centerHardwareDepth = depthTexture.SampleLevel( pointSampler, i.UV, 0 ).r;
    if ( centerHardwareDepth >= 0.99999f )
        return 1.0f;

    float centerDepth = LinearizeDepth( centerHardwareDepth );
    float centerInvZ  = 1.0f / max( centerDepth, 0.00001f );
    float2 delta = cb_BlurParams.xy * cb_BlurParams.zw;

    // Slope-aware bilateral: weight neighbours by deviation from the gradient-*predicted* depth, not
    // centre depth, or a grazing plane's gradient reads as an edge and rejects everything (AO jitter).
    // Slope is in 1/Z (perspective-linear across a plane, so exact), converted back to linear Z for the
    // sharpness units. Min-magnitude one-sided difference: on a plane both sides agree, at an edge the
    // clean side wins
    float invZPlus  = 1.0f / max( LinearizeDepth( depthTexture.SampleLevel( pointSampler, i.UV + delta, 0 ).r ), 0.00001f );
    float invZMinus = 1.0f / max( LinearizeDepth( depthTexture.SampleLevel( pointSampler, i.UV - delta, 0 ).r ), 0.00001f );
    float slopePlus  = invZPlus  - centerInvZ;
    float slopeMinus = centerInvZ - invZMinus;
    float slopeInvZ  = abs( slopePlus ) < abs( slopeMinus ) ? slopePlus : slopeMinus;

    float totalAO = aoTexture.SampleLevel( pointSampler, i.UV, 0 ).r;
    float totalWeight = 1.0f;

    [unroll]
    for ( int r = 1; r <= 4; r++ )
    {
        float kernel = exp2( -(float)( r * r ) / 8.0f );

        [unroll]
        for ( int side = -1; side <= 1; side += 2 )
        {
            float2 sampleUV = i.UV + delta * (float)( r * side );
            float sampleDepth = LinearizeDepth( depthTexture.SampleLevel( pointSampler, sampleUV, 0 ).r );
            float expectedInvZ  = centerInvZ + slopeInvZ * (float)( r * side );
            float expectedDepth = 1.0f / max( expectedInvZ, 0.00001f );
            float depthWeight = exp2( -abs( sampleDepth - expectedDepth ) * cb_DepthParams.z );
            float weight = kernel * depthWeight;

            totalAO += aoTexture.SampleLevel( linearSampler, sampleUV, 0 ).r * weight;
            totalWeight += weight;
        }
    }

    // Return scalar -- the RT is R16_FLOAT so only the R channel is stored.
    return totalAO / totalWeight;
}
