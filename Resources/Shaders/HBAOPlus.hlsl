#include "ScreenSpace.hlsl"

Texture2D    depthTexture : register( t0 );
SamplerState pointSampler : register( s0 );

cbuffer HBAOCBuffer : register( b1 )
{
    float4 cb_Projection;      // m11, m22, m31, m32
    float4 cb_DepthParams;     // m33, m43, near, far
    float4 cb_Params;          // radius, bias, intensity, power
    float4 cb_BufferSize;      // width, height, invWidth, invHeight
};

static const int NUM_DIRECTIONS = 8;
static const int NUM_STEPS      = 4;

static const float2 BASE_DIRS[8] =
{
    float2(  1.00000f,  0.00000f ),
    float2(  0.70711f,  0.70711f ),
    float2(  0.00000f,  1.00000f ),
    float2( -0.70711f,  0.70711f ),
    float2( -1.00000f,  0.00000f ),
    float2( -0.70711f, -0.70711f ),
    float2(  0.00000f, -1.00000f ),
    float2(  0.70711f, -0.70711f ),
};

float LinearizeDepth( float hardwareDepth )
{
    float invNear = 1.0f / max( cb_DepthParams.z, 0.00001f );
    float invFar = 1.0f / max( cb_DepthParams.w, cb_DepthParams.z + 0.00001f );
    return 1.0f / ( hardwareDepth * ( invFar - invNear ) + invNear );
}

float3 UVToViewFromDepth( float2 uv, float hardwareDepth )
{
    float viewDepth = LinearizeDepth( hardwareDepth );
    float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );

    float focalX = max( abs( cb_Projection.x ), 0.0001f );
    float focalY = max( abs( cb_Projection.y ), 0.0001f );
    float viewX = ( ndc.x - cb_Projection.z ) * viewDepth / focalX;
    float viewY = ( ndc.y - cb_Projection.w ) * viewDepth / focalY;
    return float3( viewX, viewY, viewDepth );
}

float3 UVToView( float2 uv )
{
    return UVToViewFromDepth( uv, depthTexture.SampleLevel( pointSampler, uv, 0 ).r );
}

float3 ReconstructNormal( float2 uv, float3 center )
{
    // Use forward-difference only (2 depth reads instead of 4).
    // MinDiff quality gain at depth edges is negligible for AO; the bandwidth
    // saving (half the neighbour samples) is worth the minor bias.
    float2 texel = cb_BufferSize.zw;
    float3 pr = UVToView( uv + float2( texel.x, 0.0f ) );
    float3 pt = UVToView( uv + float2( 0.0f, texel.y ) );

    float3 dx = pr - center;
    float3 dy = pt - center;
    return normalize( cross( dx, dy ) );
}

float InterleavedGradientNoise( float2 pixel )
{
    return frac( 52.9829189f * frac( dot( pixel, float2( 0.06711056f, 0.00583715f ) ) ) );
}

float ComputeSampleAO( float3 center, float3 normal, float3 samplePos, float radius, float bias )
{
    float3 v = samplePos - center;
    float distSq = dot( v, v );
    float invDist = rsqrt( max( distSq, 0.00001f ) );
    float nDotV = dot( normal, v ) * invDist;
    float falloff = saturate( 1.0f - distSq / ( radius * radius ) );
    return saturate( nDotV - bias ) * falloff;
}

float ps_main( PS_IN i ) : SV_TARGET
{
    float hardwareDepth = depthTexture.SampleLevel( pointSampler, i.UV, 0 ).r;
    if ( hardwareDepth >= 0.99999f )
        return (float)1.0f;

    float3 center = UVToViewFromDepth( i.UV, hardwareDepth );
    float3 normal = ReconstructNormal( i.UV, center );

    float radius = max( cb_Params.x, 0.0001f );
    float bias = cb_Params.y;
    float focalY = max( abs( cb_Projection.y ), 0.0001f );
    float radiusPixels = clamp( ( radius * focalY ) / max( center.z, 0.0001f ) * cb_BufferSize.y * 0.5f, 2.0f, 384.0f );
    float stepPixels = radiusPixels / (float)( NUM_STEPS + 1 );
    float rotation   = InterleavedGradientNoise( i.Position.xy ) * 6.28318530718f;
    float stepJitter = InterleavedGradientNoise( i.Position.xy + float2( 0.0f, 31.41f ) );

    float sinR, cosR;
    sincos( rotation, sinR, cosR );

    float smallScaleAO = 0.0f;
    float largeScaleAO = 0.0f;

    [unroll]
    for ( int dirIndex = 0; dirIndex < NUM_DIRECTIONS; dirIndex++ )
    {
        float2 base = BASE_DIRS[ dirIndex ];
        float2 dir  = float2( cosR * base.x - sinR * base.y, sinR * base.x + cosR * base.y );

        [unroll]
        for ( int stepIndex = 1; stepIndex <= NUM_STEPS; stepIndex++ )
        {
            float samplePixels = 1.0f + ( (float)stepIndex - 0.5f + stepJitter ) * stepPixels;
            float2 sampleUV = i.UV + dir * samplePixels * cb_BufferSize.zw;

            sampleUV = clamp( sampleUV, 0.0f, 1.0f );
            float3 samplePos = UVToView( sampleUV );
            float sampleAO = ComputeSampleAO( center, normal, samplePos, radius, bias );
            if ( stepIndex == 1 )
                smallScaleAO += sampleAO;
            else
                largeScaleAO += sampleAO;
        }
    }

    float amountScale = cb_Params.z / max( 1.0f - bias, 0.0001f );
    float ao = smallScaleAO * ( amountScale * 2.0f ) + largeScaleAO * amountScale;
    ao /= (float)( NUM_DIRECTIONS * ( NUM_STEPS + 1 ) );
    ao = saturate( 1.0f - ao );
    ao = pow( ao, cb_Params.w );

    // Return scalar -- the RT is R16_FLOAT so only the R channel is stored.
    return ao;
}
