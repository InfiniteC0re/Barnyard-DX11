// Half-res depth downsample for screen-space passes (SSR marches against half-res depth).
// Takes the min of each 2x2 quad: conservative for ray marching -- a ray stops at the closest
// surface in the quad, so thin foreground geometry is preserved instead of averaged away

#include "ScreenSpace.hlsl"

Texture2D    depthTexture : register( t0 );
SamplerState pointSampler : register( s0 );

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float4 d = depthTexture.GatherRed( pointSampler, i.UV );
    float  m = min( min( d.x, d.y ), min( d.z, d.w ) );
    return float4( m, m, m, m );
}
