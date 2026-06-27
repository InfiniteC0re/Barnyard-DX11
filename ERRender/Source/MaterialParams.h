#pragma once

#include <Render/TMaterial.h>
#include <Toshi/T2Map.h>
#include <Toshi/TUtil.h>
#include <ToshiTools/tinyxml2.h>
#include <Thread/T2Mutex.h>

namespace remaster
{

static constexpr TUINT32 MATERIAL_PARAMS_SLOT = 5;

struct MaterialParams
{
	TFLOAT fReflectivity;      // SSR reflectivity
	TFLOAT fRoughness;         // reflection blur amount (used when no roughness map)
	TFLOAT fFresnelPower;      // SSR Fresnel exponent
	TFLOAT fSpecularIntensity; // sun specular highlight strength
	TFLOAT fSpecularPower;     // specular shininess exponent
	TFLOAT fNormalStrength;    // normal-map perturbation scale (1 = as authored, 0 = flat)
	TFLOAT fRoughnessStrength; // roughness-map multiplier (1 = as authored)
	TFLOAT fParallaxScale;     // parallax occlusion depth (0 = off, ~0.02-0.08 typical)
	TCHAR  szNormalMap[ 64 ];  // normal-map file under Data\Textures (empty = none)
	TCHAR  szRoughnessMap[ 64 ];
	TCHAR  szHeightMap[ 64 ];  // height map (white = raised) for parallax
	void*  pNormalMap;         // resolved ID3D11ShaderResourceView* (slot copy only)
	void*  pRoughnessMap;
	void*  pHeightMap;
};

static constexpr const TCHAR* MATERIAL_PARAMS_PATH = "Data\\MaterialParams.xml";

// Root folder the XML map names are resolved against. Defined in ERRenderWrapper.cpp.
static constexpr const TCHAR* TEXTURE_DIR = "Data\\Textures\\";

// Load a texture file (Data\Textures\<name>) into a cached ID3D11ShaderResourceView,
// returning the same view for repeated names. Returns TNULL if the file is missing/bad.
// Defined in ERRenderWrapper.cpp (needs the D3D11 device + DirectXTex).
void* LoadCachedTexture( const TCHAR* a_szName );

// Release all cached map SRVs so the next resolve re-reads the files (hot-reload).
void ClearTextureCache();

// (Re)apply params to every live material in the engine's pool (catches materials that
// exist but weren't matched at creation, e.g. newly-added XML entries). In ERRenderWrapper.
void ApplyParamsToAllMaterials();

struct MaterialHashComparator
{
	TINT operator()( TUINT32 a, TUINT32 b ) const { return ( a < b ) ? -1 : ( ( a > b ) ? 1 : 0 ); }
};

inline Toshi::T2Map<TUINT32, MaterialParams, MaterialHashComparator>& GetMaterialParamsDB()
{
	static Toshi::T2Map<TUINT32, MaterialParams, MaterialHashComparator> s_oDB;
	return s_oDB;
}

inline Toshi::T2Map<Toshi::TMaterial*, TUINT32>& GetMaterialRegistry()
{
	static Toshi::T2Map<Toshi::TMaterial*, TUINT32> s_oRegistry;
	return s_oRegistry;
}

// Serializes all param mutations. Materials are created on the asset-streaming thread
// (AAssetStreaming), so AttachMaterialParams runs off the main thread while the ImGui
// "Reload Materials" button runs on it. Both touch the non-thread-safe T2Map caches.
inline Toshi::T2Mutex& GetMaterialParamsMutex()
{
	static Toshi::T2Mutex s_oMutex;
	return s_oMutex;
}

inline TUINT32 HashMaterialName( const TCHAR* a_szName )
{
	static const TBOOL s_bCRCReady = ( Toshi::TUtil::CRCInitialise(), TTRUE );

	TUINT32 uLength = 0;
	while ( a_szName[ uLength ] != '\0' )
		uLength++;

	return Toshi::TUtil::CRC32( (void*)a_szName, uLength );
}

// Parse the XML database. Idempotent -- safe to call again to hot-reload.
inline void LoadMaterialParamsDB( const TCHAR* a_szPath )
{
	auto& rDB = GetMaterialParamsDB();
	rDB.Clear();

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

		// Optional normal/roughness/height map file names (resolved against Data\Textures).
		if ( const TCHAR* szNormal = pElem->Attribute( "normalMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szNormalMap, szNormal, sizeof( oParams.szNormalMap ) );
		if ( const TCHAR* szRough = pElem->Attribute( "roughnessMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szRoughnessMap, szRough, sizeof( oParams.szRoughnessMap ) );
		if ( const TCHAR* szHeight = pElem->Attribute( "heightMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szHeightMap, szHeight, sizeof( oParams.szHeightMap ) );

		rDB.Insert( HashMaterialName( szName ), oParams );
	}
}

// Read the params attached to a material, or TNULL if it has none.
inline const MaterialParams* GetMaterialParams( const Toshi::TMaterial* a_pMaterial )
{
	if ( !a_pMaterial )
		return TNULL;

	auto* pSlot = reinterpret_cast<const MaterialParams*>(
	    const_cast<Toshi::TMaterial*>( a_pMaterial )->GetTexture( MATERIAL_PARAMS_SLOT )
	);

	if ( !pSlot )
		return TNULL;

	return pSlot;
}

// (Re)apply the DB params for a material given its name hash: overwrite the slot-5
// copy in place, allocate it if newly matched, or free it if no longer in the DB.
inline void ApplyParamsToMaterial( Toshi::TMaterial* a_pMaterial, TUINT32 a_uNameHash )
{
	auto&       rDB   = GetMaterialParamsDB();
	auto        it    = rDB.Find( a_uNameHash );
	auto*       pSlot = reinterpret_cast<MaterialParams*>( a_pMaterial->GetTexture( MATERIAL_PARAMS_SLOT ) );
	const TBOOL bOurs = pSlot;

	if ( rDB.IsValid( it ) )
	{
		const MaterialParams& rSrc = it.GetValue()->GetSecond();
		if ( bOurs )
		{
			*pSlot = rSrc; // overwrite in place
		}
		else if ( a_pMaterial->GetTextureNum() <= MATERIAL_PARAMS_SLOT ) // slot 5 must be free
		{
			pSlot = new MaterialParams( rSrc );
			a_pMaterial->SetTexture( MATERIAL_PARAMS_SLOT, reinterpret_cast<Toshi::TTexture*>( pSlot ) );
		}
		else
		{
			return; // slot occupied by a real texture
		}

		// Resolve the XML map names into shared cached shader resource views.
		pSlot->pNormalMap    = pSlot->szNormalMap[ 0 ]    ? LoadCachedTexture( pSlot->szNormalMap )    : TNULL;
		pSlot->pRoughnessMap = pSlot->szRoughnessMap[ 0 ] ? LoadCachedTexture( pSlot->szRoughnessMap ) : TNULL;
		pSlot->pHeightMap    = pSlot->szHeightMap[ 0 ]    ? LoadCachedTexture( pSlot->szHeightMap )    : TNULL;
	}
	else if ( bOurs )
	{
		// No longer authored -> free and clear.
		delete pSlot;
		a_pMaterial->SetTexture( MATERIAL_PARAMS_SLOT, TNULL );
	}
}

// On material creation: register it (so hot-reload can reach it) and apply params.
inline void AttachMaterialParams( Toshi::TMaterial* a_pMaterial, const TCHAR* a_szName )
{
	if ( !a_pMaterial || !a_szName )
		return;

	Toshi::T2MutexLock oLock( GetMaterialParamsMutex() );
	const TUINT32 uHash = HashMaterialName( a_szName );
	GetMaterialRegistry().Insert( a_pMaterial, uHash );
	ApplyParamsToMaterial( a_pMaterial, uHash );
}

// On material destruction: free our struct + clear the slot before the engine's own
// cleanup runs (so it is never released as a texture), and drop it from the registry.
inline void DetachMaterialParams( Toshi::TMaterial* a_pMaterial )
{
	if ( !a_pMaterial )
		return;

	Toshi::T2MutexLock oLock( GetMaterialParamsMutex() );
	auto* pSlot = reinterpret_cast<MaterialParams*>( a_pMaterial->GetTexture( MATERIAL_PARAMS_SLOT ) );
	if ( pSlot )
	{
		delete pSlot;
		a_pMaterial->SetTexture( MATERIAL_PARAMS_SLOT, TNULL );
	}

	auto& rReg = GetMaterialRegistry();
	if ( rReg.IsValid( rReg.Find( a_pMaterial ) ) )
		rReg.FindAndRemove( a_pMaterial );
}

// Re-parse the XML and re-apply to every live material in the engine pool. Safe to call
// at any time (e.g. from an ImGui "Reload Materials" button) for live iteration. Picks
// up newly-added XML entries for already-loaded materials, not just ones we registered.
inline void ReloadMaterialParams()
{
	Toshi::T2MutexLock oLock( GetMaterialParamsMutex() );
	LoadMaterialParamsDB( MATERIAL_PARAMS_PATH );
	ClearTextureCache();        // drop cached SRVs so edited normal/roughness files re-read from disk
	ApplyParamsToAllMaterials(); // walk the engine's material pool
}

} // namespace remaster
