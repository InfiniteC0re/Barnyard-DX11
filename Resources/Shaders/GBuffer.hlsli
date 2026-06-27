#ifndef GBUFFER_HLSLI
#define GBUFFER_HLSLI

float2 OctWrap( float2 v )
{
    // Sign must be applied per-component (collapsing it to one condition corrupts
    // normals in the lower hemisphere).
    float2 s = float2( v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f );
    return ( 1.0f - abs( v.yx ) ) * s;
}

// Encode a unit normal -> [0,1]^2.
float2 OctEncodeNormal( float3 n )
{
    n /= ( abs( n.x ) + abs( n.y ) + abs( n.z ) );
    float2 e = ( n.z >= 0.0f ) ? n.xy : OctWrap( n.xy );
    return e * 0.5f + 0.5f;
}

// Decode [0,1]^2 -> unit normal.
float3 OctDecodeNormal( float2 f )
{
    f = f * 2.0f - 1.0f;
    float3 n = float3( f.x, f.y, 1.0f - abs( f.x ) - abs( f.y ) );
    float  t = saturate( -n.z );
    n.x += ( n.x >= 0.0f ) ? -t : t;
    n.y += ( n.y >= 0.0f ) ? -t : t;
    return normalize( n );
}

// Pack fresnel power and roughness into a single 8-bit UNORM channel (the G-buffer is RGBA8):
//   bits 3..7 = roughness    (32 steps over [0,1], same precision as the old 16F packing)
//   bits 0..2 = fresnel power (0..7 integer; covers the authored SSR range, higher clamps)
// Max packed integer = 31*8 + 7 = 255, exactly one 8-bit channel. The /255 stores it as a
// UNORM value that the hardware round-trips losslessly back to the same integer.
float PackFresnelRoughness( float fresnelPower, float roughness )
{
    float fp = clamp( floor( fresnelPower + 0.5f ), 0.0f, 7.0f );
    float r  = floor( saturate( roughness ) * 31.0f + 0.5f );
    return ( r * 8.0f + fp ) / 255.0f;
}

void UnpackFresnelRoughness( float packed, out float fresnelPower, out float roughness )
{
    float v      = floor( packed * 255.0f + 0.5f );
    fresnelPower = fmod( v, 8.0f );
    roughness    = floor( v / 8.0f ) / 31.0f;
}

#endif // GBUFFER_HLSLI
