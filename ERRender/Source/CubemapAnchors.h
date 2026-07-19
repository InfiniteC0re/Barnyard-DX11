#pragma once
#include <Math/TVector4.h>

// Per-level sky-cube probe anchors: capturing at the live camera makes reflections swim, so the
// nearest anchor is the capture point and cb_SkyCubeOffset carries (camera - anchor)

namespace remaster
{

// Game terrain/level table (RE'd addresses): ms_peCurrentLevel is the loaded terrain index,
// ms_aTerrains maps it to the name used as the XML key
struct TerrainInfo
{
	const TCHAR* szName;
	TUINT32      uiUnk;
};

static constexpr TINT TERRAIN_NUMOF = 57;

inline static const TUINT32*     ms_peCurrentLevel = TREINTERPRETCAST( const TUINT32*, 0x00772800 );
inline static const TerrainInfo* ms_aTerrains      = TREINTERPRETCAST( const TerrainInfo*, 0x00773200 );

static constexpr TINT MAX_ANCHORS_PER_LEVEL = 48;

struct CubemapAnchor
{
	Toshi::TVector4 vPosition;    // xyz = world-space probe capture point, w = influence radius
	Toshi::TVector4 vHalfExtents; // xyz = parallax box half-extents in world units (w unused)
};

TINT         CubemapAnchors_GetCurrentLevel();
const TCHAR* CubemapAnchors_GetLevelName( TINT a_iLevel );

// Save keeps every level, not just the edited one
void CubemapAnchors_Load( const TCHAR* a_szPath );
void CubemapAnchors_Save( const TCHAR* a_szPath );

// Editing API operates on the current level's anchor set
TINT           CubemapAnchors_GetCount();
CubemapAnchor& CubemapAnchors_Get( TINT a_iIndex );
TINT           CubemapAnchors_Add( const CubemapAnchor& a_rAnchor ); // returns index, or -1 if full/no level
void           CubemapAnchors_Remove( TINT a_iIndex );

// SelectActive caches the nearest anchor and returns it; GetActive returns that cache, so call
// SelectActive once per frame first
const CubemapAnchor* CubemapAnchors_SelectActive( const Toshi::TVector4& a_vCameraPos );
const CubemapAnchor* CubemapAnchors_GetActive();

} // namespace remaster
