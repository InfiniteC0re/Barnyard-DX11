#include "ScreenSpace.hlsl"
#include "ShaderUtils.hlsli"

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

    // At a hard depth discontinuity (a silhouette edge against a far background) all four
    // low-res taps can be depth-rejected at once: every exp() depth weight underflows,
    // weightSum collapses to ~0, and the pixel ends up with no fog. Because the fog is added
    // to the scene, "no fog" reads as a dark speckle along tree/fence/wire edges. Fall back to
    // a plain bilinear fog tap there instead of dividing by ~0, so every pixel still gets fog.
    if ( weightSum < 1e-3f )
        return fogTexture.SampleLevel( fogSampler, uv, 0 ).rgb;

    return fogSum / weightSum;
}

// Temporal resolve: blend this frame's (jittered, undersampled) march with the previous
// resolved frame so the per-frame ray-jitter noise accumulates away over ~8 frames. The fog
// is screen-space with no reprojection, so a raw history blend would smear/ghost when the
// camera moves. Instead we clamp the history into the 3x3 neighbourhood min/max of the current
// fog (Karis-style neighbourhood clamp): stale history that falls outside the plausible local
// range is snapped back to it, which rejects ghosting while still letting in-range history
// denoise the result.
float4 ps_temporal( PS_IN i ) : SV_TARGET
{
    float3 current = fogTexture.SampleLevel( fogSampler, i.UV, 0 ).rgb;

    // No usable history on the first frame / after a resize, and mode 1 (alpha = 1) wants pure
    // current -- take the march straight through in both cases.
    if ( cb_CompositeParams.y < 0.5f || cb_CompositeParams.x >= 0.999f )
        return float4( current, 1.0f );

    uint width, height;
    fogTexture.GetDimensions( width, height );
    float2 texel = 1.0f / float2( (float)width, (float)height );

    float3 nmin = current;
    float3 nmax = current;
    [unroll]
    for ( int y = -1; y <= 1; y++ )
    {
        [unroll]
        for ( int x = -1; x <= 1; x++ )
        {
            if ( x == 0 && y == 0 )
                continue;
            float3 n = fogTexture.SampleLevel( fogSampler, i.UV + float2( (float)x, (float)y ) * texel, 0 ).rgb;
            nmin = min( nmin, n );
            nmax = max( nmax, n );
        }
    }

    float3 history = historyFogTexture.SampleLevel( fogSampler, i.UV, 0 ).rgb;
    history = clamp( history, nmin, nmax );

    // cb_CompositeParams.x = blend weight for the current frame (history-heavy, ~0.12).
    return float4( lerp( history, current, cb_CompositeParams.x ), 1.0f );
}

float4 ps_additive( PS_IN i ) : SV_TARGET
{
    float3 fog = SampleDepthAwareFog( i.UV );
    float4 scene = sceneTexture.SampleLevel( sceneSampler, i.UV, 0 );
    return float4( DitherR11G11B10( scene.rgb + fog, i.Position.xy ), scene.a );
}

float4 ps_darken( PS_IN i ) : SV_TARGET
{
    float darken = SampleDepthAwareFog( i.UV ).r;
    float4 scene = sceneTexture.SampleLevel( sceneSampler, i.UV, 0 );
    return float4( DitherR11G11B10( scene.rgb * darken, i.Position.xy ), scene.a );
}
