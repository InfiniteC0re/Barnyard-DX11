// FXAA 3.11 PC, preset 39. Port of Timothy Lottes' Fxaa3_11.h FXAA_PC path (NVIDIA, public domain).
// Runs on Postprocess.hlsl ps_main_aa's LDR output, which packs perceptual luma into alpha (FxaaLuma)

#include "ScreenSpace.hlsl"

Texture2D    tex        : register( t0 ); // LDR scene, .rgb = color, .a = luma
SamplerState texSampler : register( s0 ); // linear, clamp

cbuffer AAParams : register( b1 )
{
    float4 g_RTMetrics; // ( 1/width, 1/height, width, height )
};

// FXAA tunables (Lottes' PC defaults)
static const float kSubpix           = 0.75f;   // sub-pixel aliasing removal [0..1]
static const float kEdgeThreshold    = 0.166f;  // min local contrast to apply AA
static const float kEdgeThresholdMin = 0.0833f; // trims processing in dark areas

// FXAA_QUALITY__PRESET 39: 12 edge-search steps
static const float kQuality[12] =
{
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f, 2.0f, 2.0f, 2.0f, 4.0f, 8.0f
};

float FxaaLuma( float2 uv )
{
    return tex.SampleLevel( texSampler, uv, 0.0f ).w;
}

float4 ps_main( PS_IN i ) : SV_TARGET
{
    const float2 posM     = i.UV;
    const float2 rcpFrame = g_RTMetrics.xy;

    float4 rgbyM = tex.SampleLevel( texSampler, posM, 0.0f );
    float  lumaM = rgbyM.w;

    float lumaS = tex.SampleLevel( texSampler, posM, 0.0f, int2(  0,  1 ) ).w;
    float lumaE = tex.SampleLevel( texSampler, posM, 0.0f, int2(  1,  0 ) ).w;
    float lumaN = tex.SampleLevel( texSampler, posM, 0.0f, int2(  0, -1 ) ).w;
    float lumaW = tex.SampleLevel( texSampler, posM, 0.0f, int2( -1,  0 ) ).w;

    float rangeMax = max( max( lumaN, lumaS ), max( lumaE, max( lumaW, lumaM ) ) );
    float rangeMin = min( min( lumaN, lumaS ), min( lumaE, min( lumaW, lumaM ) ) );
    float range    = rangeMax - rangeMin;

    if ( range < max( kEdgeThresholdMin, rangeMax * kEdgeThreshold ) )
        return float4( rgbyM.rgb, 1.0f );

    float lumaNW = tex.SampleLevel( texSampler, posM, 0.0f, int2( -1, -1 ) ).w;
    float lumaNE = tex.SampleLevel( texSampler, posM, 0.0f, int2(  1, -1 ) ).w;
    float lumaSW = tex.SampleLevel( texSampler, posM, 0.0f, int2( -1,  1 ) ).w;
    float lumaSE = tex.SampleLevel( texSampler, posM, 0.0f, int2(  1,  1 ) ).w;

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float subpixRcpRange = 1.0f / range;
    float subpixNSWE     = lumaNS + lumaWE;

    float edgeHorz1 = ( -2.0f * lumaM ) + lumaNS;
    float edgeVert1 = ( -2.0f * lumaM ) + lumaWE;

    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float edgeHorz2 = ( -2.0f * lumaE ) + lumaNESE;
    float edgeVert2 = ( -2.0f * lumaN ) + lumaNWNE;

    float lumaNWSW = lumaNW + lumaSW;
    float lumaSWSE = lumaSW + lumaSE;
    float edgeHorz4 = ( abs( edgeHorz1 ) * 2.0f ) + abs( edgeHorz2 );
    float edgeVert4 = ( abs( edgeVert1 ) * 2.0f ) + abs( edgeVert2 );
    float edgeHorz3 = ( -2.0f * lumaW ) + lumaNWSW;
    float edgeVert3 = ( -2.0f * lumaS ) + lumaSWSE;
    float edgeHorz  = abs( edgeHorz3 ) + edgeHorz4;
    float edgeVert  = abs( edgeVert3 ) + edgeVert4;

    float subpixNWSWNESE = lumaNWSW + lumaNESE;
    float lengthSign     = rcpFrame.x;
    bool  horzSpan       = edgeHorz >= edgeVert;
    float subpixA        = subpixNSWE * 2.0f + subpixNWSWNESE;

    if ( !horzSpan ) lumaN = lumaW;
    if ( !horzSpan ) lumaS = lumaE;
    if (  horzSpan ) lengthSign = rcpFrame.y;
    float subpixB = ( subpixA * ( 1.0f / 12.0f ) ) - lumaM;

    float gradientN = lumaN - lumaM;
    float gradientS = lumaS - lumaM;
    float lumaNN    = lumaN + lumaM;
    float lumaSS    = lumaS + lumaM;
    bool  pairN     = abs( gradientN ) >= abs( gradientS );
    float gradient  = max( abs( gradientN ), abs( gradientS ) );
    if ( pairN ) lengthSign = -lengthSign;
    float subpixC = saturate( abs( subpixB ) * subpixRcpRange );

    float2 posB = posM;
    float2 offNP;
    offNP.x = ( !horzSpan ) ? 0.0f : rcpFrame.x;
    offNP.y = (  horzSpan ) ? 0.0f : rcpFrame.y;
    if ( !horzSpan ) posB.x += lengthSign * 0.5f;
    if (  horzSpan ) posB.y += lengthSign * 0.5f;

    float2 posN = posB - offNP * kQuality[ 0 ];
    float2 posP = posB + offNP * kQuality[ 0 ];
    float subpixD = ( -2.0f * subpixC ) + 3.0f;
    float lumaEndN = FxaaLuma( posN );
    float subpixE = subpixC * subpixC;
    float lumaEndP = FxaaLuma( posP );

    if ( !pairN ) lumaNN = lumaSS;
    float gradientScaled = gradient * ( 1.0f / 4.0f );
    float lumaMM = lumaM - lumaNN * 0.5f;
    float subpixF = subpixD * subpixE;
    bool  lumaMLTZero = lumaMM < 0.0f;

    lumaEndN -= lumaNN * 0.5f;
    lumaEndP -= lumaNN * 0.5f;
    bool doneN = abs( lumaEndN ) >= gradientScaled;
    bool doneP = abs( lumaEndP ) >= gradientScaled;
    if ( !doneN ) posN -= offNP * kQuality[ 1 ];
    if ( !doneP ) posP += offNP * kQuality[ 1 ];
    bool doneNP = ( !doneN ) || ( !doneP );

    [unroll]
    for ( int iStep = 2; iStep < 12; iStep++ )
    {
        if ( !doneNP ) break;

        if ( !doneN ) lumaEndN = FxaaLuma( posN ) - lumaNN * 0.5f;
        if ( !doneP ) lumaEndP = FxaaLuma( posP ) - lumaNN * 0.5f;
        doneN = abs( lumaEndN ) >= gradientScaled;
        doneP = abs( lumaEndP ) >= gradientScaled;
        if ( !doneN ) posN -= offNP * kQuality[ iStep ];
        if ( !doneP ) posP += offNP * kQuality[ iStep ];
        doneNP = ( !doneN ) || ( !doneP );
    }

    float dstN = horzSpan ? ( posM.x - posN.x ) : ( posM.y - posN.y );
    float dstP = horzSpan ? ( posP.x - posM.x ) : ( posP.y - posM.y );

    bool  goodSpanN = ( lumaEndN < 0.0f ) != lumaMLTZero;
    bool  goodSpanP = ( lumaEndP < 0.0f ) != lumaMLTZero;
    float spanLength    = dstP + dstN;
    float spanLengthRcp = 1.0f / spanLength;
    bool  directionN    = dstN < dstP;
    float dst           = min( dstN, dstP );
    bool  goodSpan      = directionN ? goodSpanN : goodSpanP;
    float pixelOffset     = ( dst * ( -spanLengthRcp ) ) + 0.5f;
    float pixelOffsetGood = goodSpan ? pixelOffset : 0.0f;

    float subpixG = subpixF * subpixF;
    float subpixH = subpixG * kSubpix;
    float pixelOffsetSubpix = max( pixelOffsetGood, subpixH );

    float2 posFinal = posM;
    if ( !horzSpan ) posFinal.x += pixelOffsetSubpix * lengthSign;
    if (  horzSpan ) posFinal.y += pixelOffsetSubpix * lengthSign;

    return float4( tex.SampleLevel( texSampler, posFinal, 0.0f ).rgb, 1.0f );
}
