#include "ScreenSpace.hlsl"

Texture2DMS<float> depthMSAA : register( t0 );

float4 ps_main( PS_IN i ) : SV_TARGET
{
    uint width, height, numSamples;
    depthMSAA.GetDimensions( width, height, numSamples );

    int2   px    = int2( i.UV * float2( width, height ) );
    float  depth = depthMSAA.Load( px, 0 );

    return float4( depth, 0.0f, 0.0f, 1.0f );
}
