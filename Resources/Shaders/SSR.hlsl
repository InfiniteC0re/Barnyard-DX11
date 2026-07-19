#include "ScreenSpace.hlsl"
#include "GBuffer.hlsli"
#include "ShaderUtils.hlsli" // ParallaxCorrectReflection, shared with the world/skin IBL

Texture2D   depthTexture      : register( t0 );
Texture2D   colorTexture      : register( t1 );
Texture2D   reflectionTexture : register( t2 );
Texture2D   gbufferTexture    : register( t3 );
TextureCube skyCube           : register( t4 ); // active ("to") cube (mipped)
TextureCube skyCube2          : register( t5 ); // outgoing ("from") cube, sampled while cross-fading

SamplerState pointSampler  : register( s0 );
SamplerState linearSampler : register( s1 );

cbuffer SSRCBuffer : register( b1 )
{
    float4   cb_Projection;  // m11, m22, m31, m32
    float4   cb_DepthParams; // m33, m43, near, far
    float4   cb_Params;      // intensity, maxDistance, thickness, fresnelPower
    float4   cb_BufferSize;  // width, height, invWidth, invHeight
    float4   cb_MarchParams; // maxSteps, pixelStride, edgeFadePower, unused
    float4   cb_BlurParams;  // invWidth, invHeight, dirX, dirY
    float4   cb_BlurDepth;   // near, far, sharpness, unused
    float4x4 cb_WorldToView; // rotates G-buffer world normals into view space
    float4   cb_SkyHorizon;  // rgb = horizon colour avg(FORWARD,TRANSLATION), a = intensity (0 = off)
    float4   cb_SkyZenith;   // rgb = zenith colour avg(RIGHT,UP)
    float4   cb_SkyCubeParams; // x = maxMip, y = cube enable (0/1), z = intensity
    float4   cb_SkyCubeParallax;  // "to" box half-extents xyz, w = enable (0/1)
    float4   cb_SkyCubeOffset;    // xyz = camera - "to" probe (world); w = cross-fade blend (1 = fully "to")
    float4   cb_SkyCubeParallax2; // "from" box half-extents xyz
    float4   cb_SkyCubeOffset2;   // xyz = camera - "from" probe (world)
};

// No ray-miss sky/cube fallback: the forward pass (World/Skin EnvSpecular) renders the cube
// reflection at full res, and SSR only adds reflections where a ray hits geometry. skyCube/skyCube2
// (t4/t5) stay bound only for ps_debug_skycube below

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
    float pixStride = max( cb_MarchParams.y, 0.25f ); // screen-space step, in (half-res) pixels
    float thickness = max( cb_Params.z, 0.001f );
    float maxDist   = cb_Params.y;

    // Screen-space DDA march: uniform PIXEL stride so sample density is even in screen space (no
    // bunching close, no gaps far). Under perspective 1/z is linear in screen space, so view depth
    // is recovered by interpolating its reciprocal along the segment
    float3 pStart = pos;
    float3 pEnd   = pos + rayDir * maxDist;

    // Clip the endpoint to the near plane so the projection (divide by view z) stays valid
    if ( pEnd.z < nearZ )
    {
        float tNear = ( nearZ - pStart.z ) / ( pEnd.z - pStart.z );
        pEnd = lerp( pStart, pEnd, saturate( tNear ) );
    }

    float2 pixStart = ViewToUV( pStart ) * cb_BufferSize.xy;
    float2 pixEnd   = ViewToUV( pEnd )   * cb_BufferSize.xy;

    // Reciprocal view depth at each end -- linear in screen space => perspective correct
    float kStart = 1.0f / max( pStart.z, 0.00001f );
    float kEnd   = 1.0f / max( pEnd.z,   0.00001f );

    float2 pixDelta = pixEnd - pixStart;
    float  pixLen   = max( abs( pixDelta.x ), abs( pixDelta.y ) );
    int    numSteps = (int)clamp( ceil( pixLen / pixStride ), 1.0f, (float)maxSteps );
    float  dt       = 1.0f / (float)numSteps;

    // Jitter the start by a fraction of a step to trade banding for noise (cleaned up by the blur)
    float jitter = InterleavedGradientNoise( i.Position.xy );

    float2 hitUV = float2( 0.0f, 0.0f );
    float  hit   = 0.0f;

    // Seed the previous depth difference just off the surface so the hit test detects a genuine
    // front->behind crossing, not the origin pixel
    float tPrev = dt * jitter;
    float lastDiff;
    {
        float2 uv0 = lerp( pixStart, pixEnd, tPrev ) * cb_BufferSize.zw;
        float  rz0 = 1.0f / lerp( kStart, kEnd, tPrev );
        float  sz0 = LinearizeDepthNF( depthTexture.SampleLevel( pointSampler, uv0, 0 ).r, nearZ, farZ );
        lastDiff   = rz0 - sz0;
    }

    [loop]
    for ( int s = 1; s <= numSteps; s++ )
    {
        float  t  = min( dt * ( (float)s + jitter ), 1.0f );
        float2 uv = lerp( pixStart, pixEnd, t ) * cb_BufferSize.zw;
        if ( any( uv < 0.0f ) || any( uv > 1.0f ) ) break;

        float rayZ = 1.0f / lerp( kStart, kEnd, t );
        if ( rayZ < nearZ ) break;

        float sceneZ = LinearizeDepthNF( depthTexture.SampleLevel( pointSampler, uv, 0 ).r, nearZ, farZ );
        float diff   = rayZ - sceneZ;

        // Hit only when the ray crosses from in front (diff <= 0) to just behind (0 < diff <
        // thickness); rejects the immediate self-hit
        if ( lastDiff <= 0.0f && diff > 0.0f && diff < thickness )
        {
            float a = tPrev;
            float b = t;
            [unroll]
            for ( int r = 0; r < 5; r++ )
            {
                float  mid   = ( a + b ) * 0.5f;
                float2 midUV = lerp( pixStart, pixEnd, mid ) * cb_BufferSize.zw;
                float  midRZ = 1.0f / lerp( kStart, kEnd, mid );
                float  midSZ = LinearizeDepthNF( depthTexture.SampleLevel( pointSampler, midUV, 0 ).r, nearZ, farZ );
                if ( midRZ - midSZ > 0.0f ) b = mid;
                else                        a = mid;
            }
            hitUV = lerp( pixStart, pixEnd, b ) * cb_BufferSize.zw;
            hit   = 1.0f;
            break;
        }

        lastDiff = diff;
        tPrev    = t;
    }

    // Fresnel: grazing angles reflect more. Floored at 4% (dielectric F0) so a head-on view keeps a
    // faint reflection instead of pow() driving it to zero
    float fresnel = pow( 1.0f - saturate( dot( -viewDir, normal ) ), fresnelPower );
    fresnel       = lerp( 0.04f, 1.0f, fresnel );

    float roughFade = 1.0f - saturate( roughness );

    float confidence = saturate( fresnel * cb_Params.x * reflectivity * roughFade );

    // Misses contribute nothing (the forward-pass cube already covers them)
    if ( hit < 0.5f )
        return float4( 0.0f, 0.0f, 0.0f, 0.0f );

    // Fade SSR out near the screen edge (back to the forward cube); folded into confidence so rgb
    // and the dst-removal fade together
    float2 edge     = abs( hitUV * 2.0f - 1.0f );
    float  edgeFade = saturate( 1.0f - pow( max( edge.x, edge.y ), max( cb_MarchParams.z, 0.0001f ) ) );
    confidence     *= edgeFade;

    // Premultiplied output (rgb pre-scaled by confidence; composite blends ONE / INV_SRC_ALPHA).
    // Needed for the bilateral blur between: straight alpha bled full-strength colour from
    // low-confidence neighbours across confidence edges (halos); premultiplied they contribute ~black
    float3 reflColor = colorTexture.SampleLevel( linearSampler, hitUV, 0 ).rgb;

    return float4( reflColor * confidence, confidence );
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
    // Roughness widens the kernel; keep a little blur even when sharp for denoise. Smaller floor than
    // the old half-res path: full res has less jitter to hide
    float  blurScale = lerp( 0.25f, 4.0f, saturate( roughness ) );
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
// Buffer is premultiplied, so rgb already carries the confidence weighting
float4 ps_debug( PS_IN i ) : SV_TARGET
{
    float4 refl = reflectionTexture.SampleLevel( linearSampler, i.UV, 0 );
    return float4( refl.rgb, 1.0f );
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

// Debug: render the sky cube as a skybox to verify face orientation. If the captured sky matches
// the real sky as the camera turns the face view matrices are correct; if mirrored/rotated/flipped,
// fix kFaces[]
float4 ps_debug_skycube( PS_IN i ) : SV_TARGET
{
    float ndcX = i.UV.x * 2.0f - 1.0f;
    float ndcY = 1.0f - i.UV.y * 2.0f;

    float m11 = abs( cb_Projection.x ) < 0.0001f ? 0.0001f : cb_Projection.x;
    float m22 = abs( cb_Projection.y ) < 0.0001f ? 0.0001f : cb_Projection.y;

    // View-space ray, rotated to world by transpose(cb_WorldToView) (orthonormal -> transpose = inverse)
    float3 viewDir  = normalize( float3( ( ndcX - cb_Projection.z ) / m11, ( ndcY - cb_Projection.w ) / m22, 1.0f ) );
    float3 worldDir = mul( viewDir, transpose( (float3x3)cb_WorldToView ) );

    return float4( skyCube.SampleLevel( linearSampler, worldDir, 0 ).rgb * cb_SkyCubeParams.z, 1.0f );
}
