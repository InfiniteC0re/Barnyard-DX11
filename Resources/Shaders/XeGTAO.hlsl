#include "ScreenSpace.hlsl"

Texture2D    depthTexture : register( t0 );
SamplerState pointSampler : register( s0 );

cbuffer XeGTAOCBuffer : register( b1 )
{
    float4 cb_Projection;      // m11, m22, m31, m32
    float4 cb_DepthParams;     // m33, m43, near, far
    float4 cb_Params;          // radius, falloffRange, intensity, finalPower
    float4 cb_BufferSize;      // width, height, invWidth, invHeight
    float4 cb_XeParams;        // radiusMultiplier, sampleDistributionPower, unused, unused
};

static const int   XE_GTAO_SLICE_COUNT          = 3;
static const int   XE_GTAO_STEPS_PER_SLICE      = 3;
static const float XE_GTAO_PIXEL_TOO_CLOSE      = 1.3f;

float LinearizeDepth( float hardwareDepth )
{
    float invNear = 1.0f / max( cb_DepthParams.z, 0.00001f );
    float invFar = 1.0f / max( cb_DepthParams.w, cb_DepthParams.z + 0.00001f );
    return 1.0f / ( hardwareDepth * ( invFar - invNear ) + invNear );
}

float3 UVToView( float2 uv )
{
    float hardwareDepth = depthTexture.SampleLevel( pointSampler, uv, 0 ).r;
    float viewDepth = LinearizeDepth( hardwareDepth );
    float focalX = max( abs( cb_Projection.x ), 0.0001f );
    float focalY = max( abs( cb_Projection.y ), 0.0001f );
    float2 ndcToViewMul = float2( 2.0f / focalX, -2.0f / focalY );
    float2 ndcToViewAdd = float2( -1.0f / focalX, 1.0f / focalY );
    float2 viewXY = ( uv * ndcToViewMul + ndcToViewAdd ) * viewDepth;
    return float3( viewXY, viewDepth );
}

float3 MinDiff( float3 center, float3 posA, float3 posB )
{
    float3 a = posA - center;
    float3 b = center - posB;
    return dot( a, a ) < dot( b, b ) ? a : b;
}

float3 ReconstructNormal( float2 uv, float3 center )
{
    float2 texel = cb_BufferSize.zw;
    float3 pr = UVToView( uv + float2( texel.x, 0.0f ) );
    float3 pl = UVToView( uv - float2( texel.x, 0.0f ) );
    float3 pt = UVToView( uv + float2( 0.0f, texel.y ) );
    float3 pb = UVToView( uv - float2( 0.0f, texel.y ) );

    float3 normal = normalize( cross( MinDiff( center, pr, pl ), MinDiff( center, pt, pb ) ) );
    return dot( normal, -center ) < 0.0f ? -normal : normal;
}

float InterleavedGradientNoise( float2 pixel )
{
    return frac( 52.9829189f * frac( dot( pixel, float2( 0.06711056f, 0.00583715f ) ) ) );
}

float IntegrateSample( float3 center, float3 normal, float3 samplePos, float falloffMul, float falloffAdd )
{
    float3 sampleDelta = samplePos - center;
    float sampleDist = length( sampleDelta );
    if ( sampleDist <= 0.0001f )
        return 0.0f;

    float horizon = saturate( dot( normal, sampleDelta / sampleDist ) );
    float weight   = saturate( sampleDist * falloffMul + falloffAdd );
    return horizon * weight;
}

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float hardwareDepth = depthTexture.SampleLevel( pointSampler, i.UV, 0 ).r;
    if ( hardwareDepth >= 0.99999f )
        return 1.0f;

    float3 center = UVToView( i.UV );
    float3 normal = ReconstructNormal( i.UV, center );

    float radius      = max( cb_Params.x * cb_XeParams.x, 0.0001f );
    float falloffRange = max( saturate( cb_Params.y ) * radius, 0.0001f );

    // weight = 1 within radius, fades to 0 at radius + falloffRange (matches reference)
    float falloffMul = -1.0f / falloffRange;
    float falloffAdd = radius / falloffRange + 1.0f;

    float sampleDistributionPower = max( cb_XeParams.y, 1.0f );

    float focalY      = max( abs( cb_Projection.y ), 0.0001f );
    float radiusPixels = clamp( ( radius * focalY ) / max( center.z, 0.0001f ) * cb_BufferSize.y * 0.5f, 2.0f, 384.0f );
    // minimum step offset in normalised [0,1] step space to guarantee >= 1.3 px from center
    float minS        = XE_GTAO_PIXEL_TOO_CLOSE / radiusPixels;

    float noise = InterleavedGradientNoise( i.Position.xy );

    float visibility  = 0.0f;
    int   sampleCount = 0;

    [unroll]
    for ( int slice = 0; slice < XE_GTAO_SLICE_COUNT; slice++ )
    {
        float sliceAngle = ( (float)slice + noise ) * ( 3.14159265359f / (float)XE_GTAO_SLICE_COUNT );
        float2 dir = float2( cos( sliceAngle ), sin( sliceAngle ) );

        [unroll]
        for ( int stepIndex = 0; stepIndex < XE_GTAO_STEPS_PER_SLICE; stepIndex++ )
        {
            // add minS before the power so even step 0 is at least 1.3 px from center,
            // preventing inconsistent skip/include behaviour at mesh seams
            float s = ( (float)stepIndex + 0.5f ) / (float)XE_GTAO_STEPS_PER_SLICE;
            s = pow( s + minS, sampleDistributionPower );

            float2 sampleOffset = round( dir * ( s * radiusPixels ) ) * cb_BufferSize.zw;

            float2 uv0 = i.UV + sampleOffset;
            float2 uv1 = i.UV - sampleOffset;

            if ( !any( uv0 < 0.0f ) && !any( uv0 > 1.0f ) )
            {
                visibility += IntegrateSample( center, normal, UVToView( uv0 ), falloffMul, falloffAdd );
                sampleCount++;
            }

            if ( !any( uv1 < 0.0f ) && !any( uv1 > 1.0f ) )
            {
                visibility += IntegrateSample( center, normal, UVToView( uv1 ), falloffMul, falloffAdd );
                sampleCount++;
            }
        }
    }

    float occlusion = sampleCount > 0 ? saturate( visibility / (float)sampleCount ) : 0.0f;
    float ao = saturate( 1.0f - occlusion * cb_Params.z );
    ao = pow( ao, cb_Params.w );

    return float4( ao, ao, ao, 1.0f );
}
