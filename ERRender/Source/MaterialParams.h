#pragma once

#include <Render/TMaterial.h>
#include <Toshi/T2Map.h>
#include <Toshi/TUtil.h>
#include <ToshiTools/tinyxml2.h>

namespace remaster
{

static constexpr TUINT32 MATERIAL_PARAMS_SLOT = 5;

struct MaterialParams
{
	TFLOAT fReflectivity;
	TFLOAT fRoughness;         // reflection blur amount
	TFLOAT fFresnelPower;      // SSR Fresnel exponent
	TFLOAT fSpecularIntensity; // sun specular highlight strength
	TFLOAT fSpecularPower;     // specular shininess exponent
};

static constexpr const TCHAR* MATERIAL_PARAMS_PATH = "Data\\MaterialParams.xml";

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
		oParams.fFresnelPower      = pElem->FloatAttribute( "fresnelPower", 4.0f );
		oParams.fSpecularIntensity = pElem->FloatAttribute( "specularIntensity", 0.0f );
		oParams.fSpecularPower     = pElem->FloatAttribute( "specularPower", 32.0f );
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
			*pSlot = rSrc; // overwrite in place (magic preserved from rSrc)
		}
		else if ( a_pMaterial->GetTextureNum() <= MATERIAL_PARAMS_SLOT ) // slot 5 must be free
		{
			a_pMaterial->SetTexture( MATERIAL_PARAMS_SLOT, reinterpret_cast<Toshi::TTexture*>( new MaterialParams( rSrc ) ) );
		}
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

// Re-parse the XML and re-apply to every registered material. Safe to call at any
// time (e.g. an ImGui "Reload Materials" button) for live iteration.
inline void ReloadMaterialParams()
{
	LoadMaterialParamsDB( MATERIAL_PARAMS_PATH );

	auto& rReg = GetMaterialRegistry();
	for ( auto it = rReg.Begin(); it != rReg.End(); it++ )
		ApplyParamsToMaterial( it.GetValue()->GetFirst(), it.GetValue()->GetSecond() );
}

} // namespace remaster
