#pragma once
#include <d3d11.h>
#include <Math/TVector4.h>

// Two reflection cubes ping-pong to cross-fade anchor switches: on a switch the old cube is
// frozen as "from" and flBlend ramps 0..1; steady state is flBlend == 1, sampling only "to"

namespace remaster
{

struct SkyCubeBlendState
{
	ID3D11ShaderResourceView* pSRVTo   = TNULL; // active/incoming cube
	ID3D11ShaderResourceView* pSRVFrom = TNULL; // frozen outgoing cube, sampled mid-transition
	Toshi::TVector4           vProbeTo;   // xyz = probe centre the "to" cube was captured at
	Toshi::TVector4           vProbeFrom; // xyz = probe centre the "from" cube was captured at
	Toshi::TVector4           vBoxTo;     // xyz = parallax box half-extents for "to"
	Toshi::TVector4           vBoxFrom;   // xyz = parallax box half-extents for "from"
	TFLOAT                    flBlend = 1.0f; // 0..1, 1 = fully "to"
};

extern SkyCubeBlendState g_oSkyCubeBlend;

} // namespace remaster
