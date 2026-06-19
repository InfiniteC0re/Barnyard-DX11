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

// Pack fresnel power (0..63, integer steps) and roughness (0..1, 32 steps) into one
// channel. Max value 31*64+63 = 2047, exactly representable in 16F.
float PackFresnelRoughness( float fresnelPower, float roughness )
{
    float fp = clamp( floor( fresnelPower + 0.5f ), 0.0f, 63.0f );
    float r  = floor( saturate( roughness ) * 31.0f + 0.5f );
    return r * 64.0f + fp;
}

void UnpackFresnelRoughness( float packed, out float fresnelPower, out float roughness )
{
    fresnelPower = fmod( packed, 64.0f );
    roughness    = floor( packed / 64.0f ) / 31.0f;
}

#endif // GBUFFER_HLSLI
