#include "Tonemap.hlsli"
#include "ScreenSpace.hlsl"

Texture2D    skyMask        : register( t0 );
SamplerState skyMaskSampler : register( s0 );

cbuffer SunShaftsCBuffer : register( b1 )
{
    float2 cb_vSunPos;
    float  cb_fSunAlpha;
    float  cb_fRaysLength;
    float3 cb_vRaysTint;
    float  cb_PADDING;
};

static const int   SAMPLE_COUNT    = 96;
static const float INV_SAMPLE_COUNT = 1.0f / (float)SAMPLE_COUNT;

float4 ps_main( PS_IN i ) : SV_TARGET
{
    float2 uv    = i.UV;
    float2 uvDir = ( uv - cb_vSunPos ) * ( cb_fRaysLength * INV_SAMPLE_COUNT );

    float3 rayColor = (float3)0.0f;

    // [loop] lets the compiler overlap texture fetches with ALU from prior iteration
    [loop]
    for ( int s = 1; s <= SAMPLE_COUNT; ++s )
    {
        float  sampleFrac     = (float)s * INV_SAMPLE_COUNT;
        // sqrt is an intrinsic; pow(..., 0.5) compiles to a transcendental on most drivers
        float  fadeTowardsSky = 1.0f - sqrt( sampleFrac );
        float3 maskSample     = skyMask.SampleLevel( skyMaskSampler, uv, 0 ).rgb;

        // Fade samples that march close to the screen border so the edge doesn't cut hard
        float2 borderDist = min( uv, 1.0f - uv );
        float  borderFade = saturate( min( borderDist.x, borderDist.y ) / 0.05f );

        rayColor += maskSample * fadeTowardsSky * borderFade;
        uv       -= uvDir;
    }

    rayColor = Uncharted2Tonemap( rayColor * cb_vRaysTint * cb_fSunAlpha );

    // No dither here on purpose -- the Kawase blur that follows would average it away. The shaft is
    // dithered at its final additive store instead (CopyTexture.hlsl, post-blur)
    return float4( rayColor, 1.0f );
}
