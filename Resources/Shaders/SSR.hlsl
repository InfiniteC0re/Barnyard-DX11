#include "ScreenSpace.hlsl"
#include "GBuffer.hlsli"

Texture2D depthTexture      : register( t0 );
Texture2D colorTexture      : register( t1 );
Texture2D reflectionTexture : register( t2 );
Texture2D gbufferTexture    : register( t3 );

SamplerState pointSampler  : register( s0 );
SamplerState linearSampler : register( s1 );

cbuffer SSRCBuffer : register( b1 )
{
    float4   cb_Projection;  // m11, m22, m31, m32
    float4   cb_DepthParams; // m33, m43, near, far
    float4   cb_Params;      // intensity, maxDistance, thickness, fresnelPower
    float4   cb_BufferSize;  // width, height, invWidth, invHeight
    float4   cb_MarchParams; // maxSteps, stepSize, edgeFadePower, unused
    float4   cb_BlurParams;  // invWidth, invHeight, dirX, dirY
    float4   cb_BlurDepth;   // near, far, sharpness, unused
    float4x4 cb_WorldToView; // rotates G-buffer world normals into view space
};

float LinearizeDepthNF( float hardwareDepth, float nearZ, float farZ )
{
    float invNear = 1.0f / max( nearZ, 0.00001f );
    float invFar  = 1.0f / max( farZ, nearZ + 0.00001f );
    return 1.0f / ( hardwareDepth * ( invFar - invNear ) + invNear );
}

// Reconstruct a view-space position from a screen UV. This MUST be the exact
// inverse of ViewToUV below -- in particular it has to undo the m31/m32 projection
// offset, or off-centre projections (as this engine uses, see the sun-shafts
// projection in ERRenderWrapper) leave the reconstructed position misaligned from
// where it re-projects on screen, which breaks the SSR depth comparison.
float3 UVToView( float2 uv )
{
    float hardwareDepth = depthTexture.SampleLevel( pointSampler, uv, 0 ).r;
    float viewDepth = LinearizeDepthNF( hardwareDepth, cb_DepthParams.z, cb_DepthParams.w );

    float ndcX = uv.x * 2.0f - 1.0f;
    float ndcY = 1.0f - uv.y * 2.0f;

    float m11 = abs( cb_Projection.x ) < 0.0001f ? 0.0001f : cb_Projection.x;
    float m22 = abs( cb_Projection.y ) < 0.0001f ? 0.0001f : cb_Projection.y;

    float vx = ( ndcX - cb_Projection.z ) / m11 * viewDepth;
    float vy = ( ndcY - cb_Projection.w ) / m22 * viewDepth;
    return float3( vx, vy, viewDepth );
}

// Project a view-space position back to screen UV (matches the engine's forward
// projection used by the sun shafts, including the m31/m32 offset).
float2 ViewToUV( float3 viewPos )
{
    float invZ = 1.0f / max( viewPos.z, 0.00001f );
    float ndcX = ( viewPos.x * invZ ) * cb_Projection.x + cb_Projection.z;
    float ndcY = ( viewPos.y * invZ ) * cb_Projection.y + cb_Projection.w;
    return float2( ndcX * 0.5f + 0.5f, ( 1.0f - ndcY ) * 0.5f );
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

// Gather pass: march the reflection ray. Output rgb = reflected colour, a = confidence.
float4 ps_gather( PS_IN i ) : SV_TARGET
{
    float hardwareDepth = depthTexture.SampleLevel( pointSampler, i.UV, 0 ).r;
    if ( hardwareDepth >= 0.99999f )
        return float4( 0.0f, 0.0f, 0.0f, 0.0f ); // sky: nothing to reflect

    // Only authored-reflective surfaces (reflectivity > 0) cast SSR. This both limits
    // reflections to the right materials and skips the march everywhere else.
    float4 gbuffer      = gbufferTexture.SampleLevel( pointSampler, i.UV, 0 );
    float  reflectivity = gbuffer.b;
    if ( reflectivity <= 0.001f )
        return float4( 0.0f, 0.0f, 0.0f, 0.0f );

    // Unpack per-material fresnel power + roughness; decode the octahedral normal.
    float fresnelPower, roughness;
    UnpackFresnelRoughness( gbuffer.a, fresnelPower, roughness );
    fresnelPower = max( fresnelPower, 0.0001f );

    float3 worldNormal = OctDecodeNormal( gbuffer.rg );

    float3 pos     = UVToView( i.UV );
    float3 normal  = normalize( mul( worldNormal, (float3x3)cb_WorldToView ) ); // world normal -> view space
    float3 viewDir = normalize( pos );            // camera (origin) -> surface
    float3 rayDir  = reflect( viewDir, normal );  // reflection direction (view space)

    float nearZ     = cb_DepthParams.z;
    float farZ      = cb_DepthParams.w;
    int   maxSteps  = (int)cb_MarchParams.x;
    float stepSize  = max( cb_MarchParams.y, 0.001f );
    float thickness = max( cb_Params.z, 0.001f );
    float maxDist   = cb_Params.y;

    // Jitter the start to trade banding for noise (cleaned up by the blur).
    float  jitter  = InterleavedGradientNoise( i.Position.xy );
    float3 rayStep = rayDir * stepSize;

    // Start at least one (jittered) step off the surface to avoid self-intersection,
    // and seed the previous depth difference from that first position so the hit test
    // below detects a genuine front-to-behind *crossing* rather than the origin pixel.
    float3 rayPos = pos + rayStep * ( 1.0f + jitter );
    float  lastDiff;
    {
        float2 uv0 = ViewToUV( rayPos );
        float  z0  = LinearizeDepthNF( depthTexture.SampleLevel( pointSampler, uv0, 0 ).r, nearZ, farZ );
        lastDiff   = rayPos.z - z0;
    }

    float2 hitUV = float2( 0.0f, 0.0f );
    float  hit   = 0.0f;

    [loop]
    for ( int s = 0; s < maxSteps; s++ )
    {
        rayPos += rayStep;

        if ( rayPos.z < nearZ )                  break; // behind the near plane
        if ( distance( rayPos, pos ) > maxDist )  break; // ray travelled too far

        float2 uv = ViewToUV( rayPos );
        if ( any( uv < 0.0f ) || any( uv > 1.0f ) ) break; // left the screen

        float sceneZ = LinearizeDepthNF( depthTexture.SampleLevel( pointSampler, uv, 0 ).r, nearZ, farZ );
        float diff   = rayPos.z - sceneZ;

        // Hit only when the ray crosses from in front of a surface (diff <= 0) to just
        // behind it (0 < diff < thickness). This rejects the immediate self-hit.
        if ( lastDiff <= 0.0f && diff > 0.0f && diff < thickness )
        {
            // Binary-search refine between the last (front) and current (behind) sample.
            float3 a = rayPos - rayStep;
            float3 b = rayPos;
            [unroll]
            for ( int r = 0; r < 5; r++ )
            {
                float3 mid  = ( a + b ) * 0.5f;
                float  midZ = LinearizeDepthNF( depthTexture.SampleLevel( pointSampler, ViewToUV( mid ), 0 ).r, nearZ, farZ );
                if ( mid.z - midZ > 0.0f ) b = mid;
                else                       a = mid;
            }
            hitUV = ViewToUV( b );
            hit   = 1.0f;
            break;
        }

        lastDiff = diff;
    }

    if ( hit < 0.5f )
        return float4( 0.0f, 0.0f, 0.0f, 0.0f );

    // Fresnel: grazing angles reflect more.
    float fresnel = pow( 1.0f - saturate( dot( -viewDir, normal ) ), fresnelPower );

    // Fade as the hit approaches the screen edge to hide the screen-space cutoff.
    float2 edge     = abs( hitUV * 2.0f - 1.0f );
    float  edgeFade = saturate( 1.0f - pow( max( edge.x, edge.y ), max( cb_MarchParams.z, 0.0001f ) ) );

    float3 reflColor = colorTexture.SampleLevel( linearSampler, hitUV, 0 ).rgb;

    // Rough surfaces scatter their reflection, so fade the sharp screen-space hit as
    // roughness rises (the blur pass also widens it). Smooth (0) keeps full strength.
    float  roughFade  = 1.0f - saturate( roughness );
    float  confidence = saturate( fresnel * edgeFade * cb_Params.x * reflectivity * roughFade );

    return float4( reflColor, confidence );
}

// Depth-aware separable bilateral blur of the reflection buffer. The blur radius
// scales with the surface's per-material roughness (read from the G-buffer).
float4 ps_blur( PS_IN i ) : SV_TARGET
{
    float4 gb     = gbufferTexture.SampleLevel( pointSampler, i.UV, 0 );
    float4 center = reflectionTexture.SampleLevel( pointSampler, i.UV, 0 );

    // Non-reflective pixels: nothing to blur (and avoids bleeding reflections onto them).
    if ( gb.b <= 0.001f )
        return center;

    float fresnelPower, roughness;
    UnpackFresnelRoughness( gb.a, fresnelPower, roughness );

    float centerDepth = LinearizeDepthNF( depthTexture.SampleLevel( pointSampler, i.UV, 0 ).r, cb_BlurDepth.x, cb_BlurDepth.y );
    // Roughness widens the kernel; keep a little blur even when sharp for SSR denoise.
    float  blurScale = lerp( 0.5f, 4.0f, saturate( roughness ) );
    float2 delta = cb_BlurParams.xy * cb_BlurParams.zw * blurScale;

    float4 total = center;
    float  totalWeight = 1.0f;

    [unroll]
    for ( int r = 1; r <= 4; r++ )
    {
        float kernel = exp2( -(float)( r * r ) / 8.0f );

        [unroll]
        for ( int side = -1; side <= 1; side += 2 )
        {
            float2 sampleUV = i.UV + delta * (float)( r * side );
            float sampleDepth = LinearizeDepthNF( depthTexture.SampleLevel( pointSampler, sampleUV, 0 ).r, cb_BlurDepth.x, cb_BlurDepth.y );
            float depthWeight = exp2( -abs( sampleDepth - centerDepth ) * cb_BlurDepth.z );
            float weight = kernel * depthWeight;

            total += reflectionTexture.SampleLevel( linearSampler, sampleUV, 0 ) * weight;
            totalWeight += weight;
        }
    }

    return total / totalWeight;
}

// Composite: output reflection colour with confidence in alpha; the CPU side sets
// SRC_ALPHA / INV_SRC_ALPHA so this lerps the reflection over the scene.
float4 ps_composite( PS_IN i ) : SV_TARGET
{
    float4 refl = reflectionTexture.SampleLevel( linearSampler, i.UV, 0 );
    return float4( refl.rgb, refl.a );
}

// Debug: show the reflection colour where confident, scene-dark elsewhere.
float4 ps_debug( PS_IN i ) : SV_TARGET
{
    float4 refl = reflectionTexture.SampleLevel( linearSampler, i.UV, 0 );
    return float4( refl.rgb * refl.a, 1.0f );
}

// Debug: visualise the G-buffer world normal as colour. If these colours stay put
// as the camera rotates, the normals are world-space (correct); if they swim with
// the camera, they're view-space.
float4 ps_debug_normal( PS_IN i ) : SV_TARGET
{
    float4 gb = gbufferTexture.SampleLevel( pointSampler, i.UV, 0 );
    float3 n  = OctDecodeNormal( gb.rg );
    return float4( n * 0.5f + 0.5f, 1.0f );
}
