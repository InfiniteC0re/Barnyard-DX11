#include "pch.h"
#include "CubemapAnchors.h"

#include <ToshiTools/tinyxml2.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static remaster::CubemapAnchor s_aAnchors[ remaster::TERRAIN_NUMOF ][ remaster::MAX_ANCHORS_PER_LEVEL ];
static TINT                    s_aiAnchorCount[ remaster::TERRAIN_NUMOF ] = {};
static const remaster::CubemapAnchor* s_pActiveAnchor = TNULL;

static TBOOL NamesEqual( const TCHAR* a, const TCHAR* b )
{
	if ( !a || !b )
		return TFALSE;

	for ( ;; ++a, ++b )
	{
		TCHAR ca = *a, cb = *b;
		if ( ca >= 'A' && ca <= 'Z' ) ca += ( 'a' - 'A' );
		if ( cb >= 'A' && cb <= 'Z' ) cb += ( 'a' - 'A' );
		if ( ca != cb )
			return TFALSE;
		if ( ca == '\0' )
			return TTRUE;
	}
}

static TINT FindLevelByName( const TCHAR* a_szName )
{
	for ( TINT i = 0; i < remaster::TERRAIN_NUMOF; i++ )
	{
		if ( NamesEqual( remaster::ms_aTerrains[ i ].szName, a_szName ) )
			return i;
	}
	return -1;
}

TINT remaster::CubemapAnchors_GetCurrentLevel()
{
	const TUINT32 uiLevel = *ms_peCurrentLevel;
	if ( uiLevel >= (TUINT32)TERRAIN_NUMOF )
		return -1;
	return (TINT)uiLevel;
}

const TCHAR* remaster::CubemapAnchors_GetLevelName( TINT a_iLevel )
{
	if ( a_iLevel < 0 || a_iLevel >= TERRAIN_NUMOF )
		return "";
	return ms_aTerrains[ a_iLevel ].szName;
}

void remaster::CubemapAnchors_Load( const TCHAR* a_szPath )
{
	for ( TINT i = 0; i < TERRAIN_NUMOF; i++ )
		s_aiAnchorCount[ i ] = 0;
	s_pActiveAnchor = TNULL;

	tinyxml2::XMLDocument oDoc;
	if ( oDoc.LoadFile( a_szPath ) != tinyxml2::XML_SUCCESS )
		return;

	const tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "cubemapAnchors" );
	if ( !pRoot )
		return;

	for ( const tinyxml2::XMLElement* pLevel = pRoot->FirstChildElement( "level" );
	      pLevel;
	      pLevel = pLevel->NextSiblingElement( "level" ) )
	{
		const TCHAR* szName = pLevel->Attribute( "name" );
		if ( !szName )
			continue;

		const TINT iLevel = FindLevelByName( szName );
		if ( iLevel < 0 )
			continue;

		for ( const tinyxml2::XMLElement* pElem = pLevel->FirstChildElement( "anchor" );
		      pElem && s_aiAnchorCount[ iLevel ] < MAX_ANCHORS_PER_LEVEL;
		      pElem = pElem->NextSiblingElement( "anchor" ) )
		{
			CubemapAnchor& rAnchor = s_aAnchors[ iLevel ][ s_aiAnchorCount[ iLevel ] ];
			rAnchor.vPosition = TVector4(
			    pElem->FloatAttribute( "x", 0.0f ),
			    pElem->FloatAttribute( "y", 0.0f ),
			    pElem->FloatAttribute( "z", 0.0f ),
			    pElem->FloatAttribute( "radius", 50.0f ) ); // w = influence radius
			rAnchor.vHalfExtents = TVector4(
			    pElem->FloatAttribute( "halfX", 40.0f ),
			    pElem->FloatAttribute( "halfY", 20.0f ),
			    pElem->FloatAttribute( "halfZ", 40.0f ),
			    0.0f );
			s_aiAnchorCount[ iLevel ]++;
		}
	}
}

void remaster::CubemapAnchors_Save( const TCHAR* a_szPath )
{
	tinyxml2::XMLDocument oDoc;
	tinyxml2::XMLElement* pRoot = oDoc.NewElement( "cubemapAnchors" );
	oDoc.InsertFirstChild( pRoot );

	for ( TINT iLevel = 0; iLevel < TERRAIN_NUMOF; iLevel++ )
	{
		if ( s_aiAnchorCount[ iLevel ] == 0 )
			continue;

		tinyxml2::XMLElement* pLevel = oDoc.NewElement( "level" );
		pLevel->SetAttribute( "name", ms_aTerrains[ iLevel ].szName );

		for ( TINT i = 0; i < s_aiAnchorCount[ iLevel ]; i++ )
		{
			const CubemapAnchor&  rAnchor = s_aAnchors[ iLevel ][ i ];
			tinyxml2::XMLElement* pElem   = oDoc.NewElement( "anchor" );
			pElem->SetAttribute( "x", rAnchor.vPosition.x );
			pElem->SetAttribute( "y", rAnchor.vPosition.y );
			pElem->SetAttribute( "z", rAnchor.vPosition.z );
			pElem->SetAttribute( "radius", rAnchor.vPosition.w );
			pElem->SetAttribute( "halfX", rAnchor.vHalfExtents.x );
			pElem->SetAttribute( "halfY", rAnchor.vHalfExtents.y );
			pElem->SetAttribute( "halfZ", rAnchor.vHalfExtents.z );
			pLevel->InsertEndChild( pElem );
		}

		pRoot->InsertEndChild( pLevel );
	}

	oDoc.SaveFile( a_szPath );
}

TINT remaster::CubemapAnchors_GetCount()
{
	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	return ( iLevel < 0 ) ? 0 : s_aiAnchorCount[ iLevel ];
}

remaster::CubemapAnchor& remaster::CubemapAnchors_Get( TINT a_iIndex )
{
	// Bad index: return slot 0/0 rather than read outside storage
	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 || a_iIndex < 0 || a_iIndex >= s_aiAnchorCount[ iLevel ] )
		return s_aAnchors[ 0 ][ 0 ];
	return s_aAnchors[ iLevel ][ a_iIndex ];
}

TINT remaster::CubemapAnchors_Add( const CubemapAnchor& a_rAnchor )
{
	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 || s_aiAnchorCount[ iLevel ] >= MAX_ANCHORS_PER_LEVEL )
		return -1;

	const TINT iNew            = s_aiAnchorCount[ iLevel ];
	s_aAnchors[ iLevel ][ iNew ] = a_rAnchor;
	s_aiAnchorCount[ iLevel ]++;
	return iNew;
}

void remaster::CubemapAnchors_Remove( TINT a_iIndex )
{
	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 || a_iIndex < 0 || a_iIndex >= s_aiAnchorCount[ iLevel ] )
		return;

	for ( TINT i = a_iIndex; i < s_aiAnchorCount[ iLevel ] - 1; i++ )
		s_aAnchors[ iLevel ][ i ] = s_aAnchors[ iLevel ][ i + 1 ];
	s_aiAnchorCount[ iLevel ]--;
	s_pActiveAnchor = TNULL; // pointers may have shifted
}

const remaster::CubemapAnchor* remaster::CubemapAnchors_SelectActive( const Toshi::TVector4& a_vCameraPos )
{
	s_pActiveAnchor = TNULL;

	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 || s_aiAnchorCount[ iLevel ] == 0 )
		return TNULL;

	TFLOAT fBestDistSq = 0.0f;
	for ( TINT i = 0; i < s_aiAnchorCount[ iLevel ]; i++ )
	{
		const CubemapAnchor& rAnchor = s_aAnchors[ iLevel ][ i ];
		const TFLOAT fDx = rAnchor.vPosition.x - a_vCameraPos.x;
		const TFLOAT fDy = rAnchor.vPosition.y - a_vCameraPos.y;
		const TFLOAT fDz = rAnchor.vPosition.z - a_vCameraPos.z;
		const TFLOAT fDistSq = fDx * fDx + fDy * fDy + fDz * fDz;

		// Influence sphere (radius = vPosition.w) is the trigger region, distinct from the parallax box; outside every sphere we fall back to a camera-placed cube
		const TFLOAT fRadius = rAnchor.vPosition.w;
		if ( fDistSq > fRadius * fRadius )
			continue;

		if ( !s_pActiveAnchor || fDistSq < fBestDistSq )
		{
			fBestDistSq     = fDistSq;
			s_pActiveAnchor = &rAnchor;
		}
	}

	return s_pActiveAnchor;
}

const remaster::CubemapAnchor* remaster::CubemapAnchors_GetActive()
{
	return s_pActiveAnchor;
}
