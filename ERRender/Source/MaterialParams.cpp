#include "pch.h"
#include "MaterialParams.h"
#include "Shader/WorldMaterial.h"
#include "Shader/SkinMaterial.h"

#include <Thread/T2Mutex.h>
#include <Toshi/TUtil.h>
#include <Toshi/T2Map.h>

#include <ToshiTools/tinyxml2.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static Toshi::T2Mutex s_oMutex;

static Toshi::T2Map<TUINT32, remaster::MaterialParams, remaster::MaterialHashComparator> s_oMaterialDB;

static void ApplyParamsToAllMaterials()
{
	for ( TUINT i = 0; i < MAX_NUM_ALLOCATED_MATERIALS; i++ )
	{
		MaterialNode& rNode = ms_oNodesAlloc[ i ];
		if ( rNode.iNumRefs == 0 || !rNode.pMaterial )
			continue; // free/empty slot

		// Strip the "ws_"/"ss_"/"gs_" shader prefix to recover the authored name.
		const TCHAR* szName = rNode.szName;
		if ( szName[ 0 ] && szName[ 1 ] && szName[ 2 ] == '_' )
			szName += 3;

		remaster::ApplyParamsToMaterial( rNode.pMaterial, remaster::HashMaterialName( szName ) );
	}
}

static void DetachMaterialParams( Toshi::TMaterial* a_pMaterial )
{
	if ( !a_pMaterial )
		return;

	T2MUTEX_LOCK_SCOPE( s_oMutex );

	remaster::MaterialParams* pMaterialParams = TNULL;
	if ( auto pWorldMaterial = TDYNAMICCAST( remaster::WorldMaterial, a_pMaterial ) )
	{
		pMaterialParams = pWorldMaterial->GetMaterialParams();
		pWorldMaterial->SetMaterialParams( TNULL );
	}
	else if ( auto pSkinMaterial = TDYNAMICCAST( remaster::SkinMaterial, a_pMaterial ) )
	{
		pMaterialParams = pSkinMaterial->GetMaterialParams();
		pSkinMaterial->SetMaterialParams( TNULL );
	}
}

static void ClearAllMaterialParams()
{
	for ( TUINT i = 0; i < MAX_NUM_ALLOCATED_MATERIALS; i++ )
	{
		MaterialNode& rNode = ms_oNodesAlloc[ i ];
		if ( rNode.iNumRefs == 0 || !rNode.pMaterial )
			continue; // free/empty slot

		DetachMaterialParams( rNode.pMaterial );
	}
}

TUINT32 remaster::HashMaterialName( const TCHAR* a_szName )
{
	static const TBOOL s_bCRCReady = ( Toshi::TUtil::CRCInitialise(), TTRUE );

	TUINT32 uLength = 0;
	while ( a_szName[ uLength ] != '\0' )
		uLength++;

	return Toshi::TUtil::CRC32( (void*)a_szName, uLength );
}

// Parse the XML database. Idempotent -- safe to call again to hot-reload.
void remaster::LoadMaterialParamsDB( const TCHAR* a_szPath )
{
	s_oMaterialDB.Clear();

	tinyxml2::XMLDocument oDoc;
	if ( oDoc.LoadFile( a_szPath ) != tinyxml2::XML_SUCCESS )
		return;

	const tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "materials" );
	if ( !pRoot )
		return;

	for ( const tinyxml2::XMLElement* pElem = pRoot->FirstChildElement( "material" );
	      pElem;
	      pElem = pElem->NextSiblingElement( "material" ) )
	{
		const TCHAR* szName = pElem->Attribute( "name" );
		if ( !szName )
			continue;

		MaterialParams oParams     = {};
		oParams.fReflectivity      = pElem->FloatAttribute( "reflectivity", 0.0f );
		oParams.fRoughness         = pElem->FloatAttribute( "roughness", 0.0f );
		oParams.fFresnelPower      = pElem->FloatAttribute( "fresnelPower", 0.0f );
		oParams.fSpecularIntensity = pElem->FloatAttribute( "specularIntensity", 0.05f );
		oParams.fSpecularPower     = pElem->FloatAttribute( "specularPower", 26.0f );
		oParams.fNormalStrength    = pElem->FloatAttribute( "normalStrength", 1.0f );
		oParams.fRoughnessStrength = pElem->FloatAttribute( "roughnessStrength", 1.0f );
		oParams.fParallaxScale     = pElem->FloatAttribute( "parallaxScale", 0.0f );
		oParams.fEmissiveIntensity = pElem->FloatAttribute( "emissiveIntensity", 1.0f );

		// Optional normal/roughness/height map file names (resolved against Data\Textures).
		if ( const TCHAR* szNormal = pElem->Attribute( "normalMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szNormalMap, szNormal, sizeof( oParams.szNormalMap ) );
		if ( const TCHAR* szRough = pElem->Attribute( "roughnessMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szRoughnessMap, szRough, sizeof( oParams.szRoughnessMap ) );
		if ( const TCHAR* szHeight = pElem->Attribute( "heightMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szHeightMap, szHeight, sizeof( oParams.szHeightMap ) );

		s_oMaterialDB.Insert( HashMaterialName( szName ), oParams );
	}
}

// (Re)apply the DB params for a material given its name hash: overwrite the slot-5
// copy in place, allocate it if newly matched, or free it if no longer in the DB.
void remaster::ApplyParamsToMaterial( Toshi::TMaterial* a_pMaterial, TUINT32 a_uNameHash )
{
	auto it = s_oMaterialDB.Find( a_uNameHash );

	if ( !s_oMaterialDB.IsValid( it ) ) return;
	auto pSlot = &it->second;

	if ( auto pWorldMaterial = TDYNAMICCAST( WorldMaterial, a_pMaterial ) )
		pWorldMaterial->SetMaterialParams( pSlot );
	else if ( auto pSkinMaterial = TDYNAMICCAST( SkinMaterial, a_pMaterial ) )
		pSkinMaterial->SetMaterialParams( pSlot );

	// Resolve the XML map names into shared cached shader resource views.
	pSlot->pNormalMap    = pSlot->szNormalMap[ 0 ] ? LoadCachedTexture( pSlot->szNormalMap ) : TNULL;
	pSlot->pRoughnessMap = pSlot->szRoughnessMap[ 0 ] ? LoadCachedTexture( pSlot->szRoughnessMap ) : TNULL;
	pSlot->pHeightMap    = pSlot->szHeightMap[ 0 ] ? LoadCachedTexture( pSlot->szHeightMap ) : TNULL;
}

// On material creation: register it (so hot-reload can reach it) and apply params.
void remaster::AttachMaterialParams( Toshi::TMaterial* a_pMaterial, const TCHAR* a_szName )
{
	if ( !a_pMaterial || !a_szName )
		return;

	T2MUTEX_LOCK_SCOPE( s_oMutex );
	const TUINT32 uHash = HashMaterialName( a_szName );

	ApplyParamsToMaterial( a_pMaterial, uHash );
}

// Re-parse the XML and re-apply to every live material in the engine pool. Safe to call
// at any time (e.g. from an ImGui "Reload Materials" button) for live iteration. Picks
// up newly-added XML entries for already-loaded materials, not just ones we registered.
void remaster::ReloadMaterialParams()
{
	T2MUTEX_LOCK_SCOPE( s_oMutex );
	ClearAllMaterialParams();
	LoadMaterialParamsDB( "Data\\MaterialParams.xml" );
	ClearTextureCache();         // drop cached SRVs so edited normal/roughness files re-read from disk
	ApplyParamsToAllMaterials(); // walk the engine's material pool
}
