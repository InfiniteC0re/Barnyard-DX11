#include "ScreenSpace.hlsl"

Texture2D    depthTexture  : register( t0 );
Texture2D    cloudTexture  : register( t1 );
Texture2DMS<float> depthTextureMSAA : register( t2 );
SamplerState pointSampler  : register( s0 );
SamplerState linearSampler : register( s1 );

cbuffer VolumetricCloudsCBuffer : register( b1 )
{
    float4x4 cb_matViewWorld;
    float4   cb_Projection;   // m11, m22, m31, m32
    float4   cb_DepthParams;  // unused, unused, near, far
    float4   cb_LightDirWorld;
    float4   cb_CameraPosWorld;
    float4   cb_CloudColor;
    float4   cb_CloudDarkColor;
    float4   cb_CloudParams0; // base height, thickness, density, coverage
    float4   cb_CloudParams1; // scale, wind speed, frame, max distance
    float4   cb_CloudParams2; // ambient, sun intensity, edge softness, detail strength
    float4   cb_CloudParams3; // step length, light sample count
};

static const int MAX_CLOUD_STEPS = 48;

float LinearizeDepth( float hwDepth )
{
    float invNear = 1.0f / max( cb_DepthParams.z, 0.00001f );
    float invFar  = 1.0f / max( cb_DepthParams.w, cb_DepthParams.z + 0.00001f );
    return 1.0f / ( hwDepth * ( invFar - invNear ) + invNear );
}

float3 UVToViewRay( float2 uv )
{
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    return float3(
        ( ndc.x - cb_Projection.z ) / cb_Projection.x,
        ( ndc.y - cb_Projection.w ) / cb_Projection.y,
        1.0f
    );
}

float Hash31( float3 p )
{
    p = frac( p * 0.1031f );
    p += dot( p, p.yzx + 33.33f );
    return frac( ( p.x + p.y ) * p.z );
}

float ValueNoise( float3 p )
{
    float3 i = floor( p );
    float3 f = frac( p );
    f = f * f * ( 3.0f - 2.0f * f );

    float n000 = Hash31( i + float3( 0.0f, 0.0f, 0.0f ) );
    float n100 = Hash31( i + float3( 1.0f, 0.0f, 0.0f ) );
    float n010 = Hash31( i + float3( 0.0f, 1.0f, 0.0f ) );
    float n110 = Hash31( i + float3( 1.0f, 1.0f, 0.0f ) );
    float n001 = Hash31( i + float3( 0.0f, 0.0f, 1.0f ) );
    float n101 = Hash31( i + float3( 1.0f, 0.0f, 1.0f ) );
    float n011 = Hash31( i + float3( 0.0f, 1.0f, 1.0f ) );
    float n111 = Hash31( i + float3( 1.0f, 1.0f, 1.0f ) );

    float nx00 = lerp( n000, n100, f.x );
    float nx10 = lerp( n010, n110, f.x );
    float nx01 = lerp( n001, n101, f.x );
    float nx11 = lerp( n011, n111, f.x );
    float nxy0 = lerp( nx00, nx10, f.y );
    float nxy1 = lerp( nx01, nx11, f.y );
    return lerp( nxy0, nxy1, f.z );
}

float FBM3( float3 p )
{
    float value = 0.0f;
    float amp   = 0.5f;
    value += ValueNoise( p ) * amp;
    p *= 2.03f;
    amp *= 0.5f;
    value += ValueNoise( p ) * amp;
    p *= 2.11f;
    amp *= 0.5f;
    value += ValueNoise( p ) * amp;
    return value;
}

float Remap01( float value, float oldMin, float oldMax )
{
    return saturate( ( value - oldMin ) / max( oldMax - oldMin, 0.0001f ) );
}

float Beer( float density )
{
    return exp( -max( density, 0.0f ) );
}

float HenyeyGreenstein( float cosTheta, float g )
{
    float g2 = g * g;
    return ( 1.0f - g2 ) / pow( max( 1.0f + g2 - 2.0f * g * cosTheta, 0.001f ), 1.5f );
}

float InterleavedGradientNoise( float2 pixel )
{
    return frac( 52.9829189f * frac( dot( pixel, float2( 0.06711056f, 0.00583715f ) ) ) );
}

float SampleCloudDensity( float3 worldPos )
{
    float baseHeight = cb_CloudParams0.x;
    float thickness  = max( cb_CloudParams0.y, 1.0f );
    float height01   = saturate( ( worldPos.y - baseHeight ) / thickness );
    float baseRamp   = smoothstep( 0.02f, 0.16f, height01 );
    float topRamp    = 1.0f - smoothstep( 0.62f, 1.0f, height01 );
    float anvilRamp  = 1.0f - smoothstep( 0.82f, 1.0f, height01 );
    float vertical   = baseRamp * lerp( topRamp, anvilRamp, 0.35f );

    float scale      = max( cb_CloudParams1.x, 0.0001f );
    float wind       = cb_CloudParams1.y * cb_CloudParams1.z;
    float2 windOffset = float2( wind, wind * 0.37f );
    float2 weatherUV = worldPos.xz * scale * 0.18f + windOffset * 0.15f;
    float weather = FBM3( float3( weatherUV, 0.37f ) );

    float3 noisePos  = float3( worldPos.xz * scale + windOffset, height01 * 2.0f ).xzy;
    float shapeLow   = FBM3( noisePos * 0.55f );
    float shapeMid   = FBM3( noisePos * 1.35f + 7.3f );
    float billow     = 1.0f - abs( shapeMid * 2.0f - 1.0f );
    float shape      = saturate( shapeLow * 0.72f + billow * 0.34f + weather * 0.42f );

    float coverage   = saturate( cb_CloudParams0.w );
    float threshold  = lerp( 0.88f, 0.24f, coverage );
    float softness   = max( cb_CloudParams2.z, 0.01f );
    float density    = Remap01( shape, threshold, threshold + softness );

    if ( density > 0.001f && cb_CloudParams2.w > 0.001f )
    {
        float detail = FBM3( noisePos * 4.4f + 11.0f );
        float erosion = detail * cb_CloudParams2.w * lerp( 0.18f, 0.78f, height01 );
        density = Remap01( density, erosion, 1.0f );
    }

    return density * vertical * max( cb_CloudParams0.z, 0.0f );
}

float SampleCloudLightVisibility( float3 worldPos, float3 sunDir )
{
    float lightSamples = clamp( cb_CloudParams3.y, 0.0f, 4.0f );
    if ( lightSamples < 0.5f )
        return 1.0f;

    float opticalDepth = 0.0f;
    if ( lightSamples > 0.5f ) opticalDepth += SampleCloudDensity( worldPos + sunDir * 32.0f ) * 0.55f;
    if ( lightSamples > 1.5f ) opticalDepth += SampleCloudDensity( worldPos + sunDir * 82.0f ) * 0.85f;
    if ( lightSamples > 2.5f ) opticalDepth += SampleCloudDensity( worldPos + sunDir * 160.0f ) * 1.10f;
    if ( lightSamples > 3.5f ) opticalDepth += SampleCloudDensity( worldPos + sunDir * 280.0f ) * 1.30f;

    return Beer( opticalDepth * 1.35f );
}

float3 EvaluateCloudLighting( float density, float stepSize, float3 dirWS, float3 sunDir, float lightVisibility )
{
    float cosTheta = dot( dirWS, sunDir );
    float forwardPhase = HenyeyGreenstein( cosTheta, 0.48f ) * 0.48f;
    float backwardPhase = HenyeyGreenstein( cosTheta, -0.22f ) * 0.18f;
    float phase = 0.42f + forwardPhase + backwardPhase;

    float powder = 1.0f - Beer( density * stepSize * 0.09f );
    float silverLining = pow( saturate( cosTheta ), 8.0f ) * lightVisibility;
    float sunLighting = cb_CloudParams2.y * lightVisibility * phase * lerp( 0.75f, 1.25f, powder );
    float ambient = cb_CloudParams2.x * lerp( 0.65f, 1.0f, saturate( cb_LightDirWorld.y * 0.5f + 0.5f ) );

    float lighting = ambient + sunLighting + silverLining * cb_CloudParams2.y * 0.35f;
    float litAmount = saturate( lightVisibility * 0.75f + powder * lightVisibility * 0.10f + lighting * 0.15f );
    float3 darkColor = cb_CloudDarkColor.rgb;
    float3 litColor  = cb_CloudColor.rgb;
    return lerp( darkColor, litColor, litAmount );
}

float GetSceneRayDistance( float2 uv, float3 dirVS )
{
    float hwDepth = depthTexture.SampleLevel( pointSampler, uv, 0 ).r;
    if ( hwDepth >= 0.99999f )
        return cb_CloudParams1.w;

    float viewDepth = LinearizeDepth( hwDepth );
    return min( viewDepth / max( dirVS.z, 0.0001f ), cb_CloudParams1.w );
}

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float3 viewRay = UVToViewRay( i.UV );
    float3 dirVS   = normalize( viewRay );
    float3 dirWS   = normalize( mul( float4( dirVS, 0.0f ), cb_matViewWorld ).xyz );
    float3 origin  = cb_CameraPosWorld.xyz;

    float baseHeight = cb_CloudParams0.x;
    float topHeight  = baseHeight + max( cb_CloudParams0.y, 1.0f );

    if ( abs( dirWS.y ) < 0.0001f )
        return 0.0f;

    float t0 = ( baseHeight - origin.y ) / dirWS.y;
    float t1 = ( topHeight  - origin.y ) / dirWS.y;
    float tEnter = max( min( t0, t1 ), 0.0f );
    float tExit  = min( max( t0, t1 ), GetSceneRayDistance( i.UV, dirVS ) );

    if ( tExit <= tEnter )
        return 0.0f;

    float rayLength = tExit - tEnter;
    float requestedStepLength = max( cb_CloudParams3.x, 4.0f );
    int stepCount = min( MAX_CLOUD_STEPS, max( 4, (int)ceil( rayLength / requestedStepLength ) ) );
    float stepSize = rayLength / (float)stepCount;
    float jitter = InterleavedGradientNoise( i.Position.xy + cb_CloudParams1.z * 17.0f );

    float transmittance = 1.0f;
    float3 cloudColor = 0.0f;
    float3 sunDir = normalize( cb_LightDirWorld.xyz );

    [loop]
    for ( int step = 0; step < MAX_CLOUD_STEPS; step++ )
    {
        if ( step >= stepCount ) break;

        float rayT = tEnter + ( (float)step + jitter ) * stepSize;
        float3 worldPos = origin + dirWS * rayT;
        float density = SampleCloudDensity( worldPos );

        if ( density > 0.0001f )
        {
            float lightVisibility = SampleCloudLightVisibility( worldPos, sunDir );
            float stepAlpha = 1.0f - Beer( density * stepSize * 0.032f );
            float3 lighting = EvaluateCloudLighting( density, stepSize, dirWS, sunDir, lightVisibility );
            cloudColor += transmittance * lighting * stepAlpha;
            transmittance *= 1.0f - stepAlpha;

            if ( transmittance < 0.01f ) break;
        }
    }

    float alpha = saturate( 1.0f - transmittance );
    return float4( cloudColor / max( alpha, 0.0001f ), alpha );
}

float4 SampleDepthAwareClouds( float2 uv )
{
    float hwDepth = depthTexture.SampleLevel( pointSampler, uv, 0 ).r;
    if ( hwDepth < 0.99999f )
        return 0.0f;

    uint cloudWidth, cloudHeight;
    cloudTexture.GetDimensions( cloudWidth, cloudHeight );

    float2 cloudSize = float2( (float)cloudWidth, (float)cloudHeight );
    float2 cloudPos  = uv * cloudSize - 0.5f;
    float2 basePos   = floor( cloudPos );
    float2 fracPos   = cloudPos - basePos;

    float4 cloudSum = 0.0f;
    float  weightSum = 0.0f;

    [unroll]
    for ( int y = 0; y < 2; y++ )
    {
        [unroll]
        for ( int x = 0; x < 2; x++ )
        {
            float2 tapPos = basePos + float2( (float)x, (float)y );
            float2 tapUV = saturate( ( tapPos + 0.5f ) / cloudSize );
            float2 bilinearWeights = lerp( 1.0f - fracPos, fracPos, float2( (float)x, (float)y ) );
            float weight = bilinearWeights.x * bilinearWeights.y;

            float tapDepth = depthTexture.SampleLevel( pointSampler, tapUV, 0 ).r;
            float skyWeight = smoothstep( 0.9995f, 1.0f, tapDepth );
            weight *= skyWeight;

            cloudSum += cloudTexture.SampleLevel( linearSampler, tapUV, 0 ) * weight;
            weightSum += weight;
        }
    }

    return cloudSum / max( weightSum, 0.0001f );
}

float4 ps_composite( PS_IN i ) : SV_TARGET
{
    return SampleDepthAwareClouds( i.UV );
}

float GetMSAASkyCoverage( float2 uv )
{
    uint depthWidth, depthHeight, sampleCount;
    depthTextureMSAA.GetDimensions( depthWidth, depthHeight, sampleCount );

    int2 pixel = clamp( int2( uv * float2( (float)depthWidth, (float)depthHeight ) ), int2( 0, 0 ), int2( (int)depthWidth - 1, (int)depthHeight - 1 ) );
    float skySamples = 0.0f;

    [loop]
    for ( uint i = 0; i < 8; i++ )
    {
        if ( i >= sampleCount )
            break;

        skySamples += step( 0.99999f, depthTextureMSAA.Load( pixel, i ) );
    }

    return skySamples / max( (float)sampleCount, 1.0f );
}

float4 SampleDepthAwareCloudsMSAA( float2 uv )
{
    float pixelSkyCoverage = GetMSAASkyCoverage( uv );
    if ( pixelSkyCoverage <= 0.0f )
        return 0.0f;

    uint cloudWidth, cloudHeight;
    cloudTexture.GetDimensions( cloudWidth, cloudHeight );

    float2 cloudSize = float2( (float)cloudWidth, (float)cloudHeight );
    float2 cloudPos  = uv * cloudSize - 0.5f;
    float2 basePos   = floor( cloudPos );
    float2 fracPos   = cloudPos - basePos;

    float4 cloudSum  = 0.0f;
    float  weightSum = 0.0f;

    [unroll]
    for ( int y = 0; y < 2; y++ )
    {
        [unroll]
        for ( int x = 0; x < 2; x++ )
        {
            float2 tapPos = basePos + float2( (float)x, (float)y );
            float2 tapUV = saturate( ( tapPos + 0.5f ) / cloudSize );
            float2 bilinearWeights = lerp( 1.0f - fracPos, fracPos, float2( (float)x, (float)y ) );
            float weight = bilinearWeights.x * bilinearWeights.y * GetMSAASkyCoverage( tapUV );

            cloudSum += cloudTexture.SampleLevel( linearSampler, tapUV, 0 ) * weight;
            weightSum += weight;
        }
    }

    float4 cloud = cloudSum / max( weightSum, 0.0001f );
    cloud.a *= pixelSkyCoverage;
    return cloud;
}

float4 ps_composite_msaa( PS_IN i ) : SV_TARGET
{
    return SampleDepthAwareCloudsMSAA( i.UV );
}
