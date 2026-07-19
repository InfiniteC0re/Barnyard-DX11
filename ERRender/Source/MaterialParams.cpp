#include "pch.h"
#include "MaterialParams.h"
#include "Shader/WorldMaterial.h"
#include "Shader/SkinMaterial.h"
#include "RenderDX11.h"

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

// Static material constants buffer (b5): every XML record baked into one immutable buffer, one
// 256-byte page each (*SetConstantBuffers1 offset granularity), bound by offset per draw. Built
// lazily on the first bind after a DB (re)load (the DB loads before the device exists)

// GPU layout of one material record; must match MaterialCB in Resources\Shaders\MaterialCB.hlsli
// Shader-side clamps (fresnel/specPower floors, envSpec>=reflectivity, parallax needs a height map) are baked in
struct MaterialGPURecord
{
	TFLOAT flReflectivity, flFresnelPower, flSpecularIntensity, flSpecularPower;
	TFLOAT flNormalStrength, flRoughnessStrength, flParallaxScale, flRoughness;
	TFLOAT flMetallic, flEnvSpecIntensity, flSpecularF0, flEmissiveIntensity;
	TFLOAT flWindMin, flWindMax, flMapFlags, flUnused;
};
TSTATICASSERT( sizeof( MaterialGPURecord ) == 64 );

static constexpr TSIZE MATBUF_PAGE_SIZE = 256; // *SetConstantBuffers1 offset granularity

static ID3D11Buffer*  s_pMaterialGPUBuffer    = TNULL; // single-buffer path (cbuffer offsetting)
static ID3D11Buffer** s_ppMaterialPageBuffers = TNULL; // one-buffer-per-page fallback
static TUINT          s_uiMaterialPageCount   = 0;
static TBOOL          s_bMaterialGPUDirty     = TTRUE;

static void ReleaseMaterialGPUBuffers()
{
	if ( s_pMaterialGPUBuffer )
	{
		s_pMaterialGPUBuffer->Release();
		s_pMaterialGPUBuffer = TNULL;
	}

	if ( s_ppMaterialPageBuffers )
	{
		for ( TUINT i = 0; i < s_uiMaterialPageCount; i++ )
			if ( s_ppMaterialPageBuffers[ i ] ) s_ppMaterialPageBuffers[ i ]->Release();

		delete[] s_ppMaterialPageBuffers;
		s_ppMaterialPageBuffers = TNULL;
	}

	s_uiMaterialPageCount = 0;
}

static void FillMaterialRecord( MaterialGPURecord& a_rRecord, const remaster::MaterialParams& a_rParams )
{
	a_rRecord.flReflectivity      = a_rParams.fReflectivity;
	a_rRecord.flFresnelPower      = TMath::Max( a_rParams.fFresnelPower, 0.1f );
	a_rRecord.flSpecularIntensity = a_rParams.fSpecularIntensity;
	a_rRecord.flSpecularPower     = TMath::Max( a_rParams.fSpecularPower, 1.0f );
	a_rRecord.flNormalStrength    = a_rParams.fNormalStrength;
	a_rRecord.flRoughnessStrength = a_rParams.fRoughnessStrength;
	// Presence flags come from the authored names, not resolved SRVs: resolving here would decode
	// every DB texture on the render thread (resolution stays lazy in ApplyParamsToMaterial)
	a_rRecord.flParallaxScale     = a_rParams.szHeightMap[ 0 ] ? a_rParams.fParallaxScale : 0.0f;
	a_rRecord.flRoughness         = a_rParams.fRoughness;
	a_rRecord.flMetallic          = a_rParams.fMetallic;
	a_rRecord.flEnvSpecIntensity  = TMath::Max( a_rParams.fEnvSpecularIntensity, a_rParams.fReflectivity );
	a_rRecord.flSpecularF0        = a_rParams.fSpecularF0;
	a_rRecord.flEmissiveIntensity = a_rParams.fEmissiveIntensity;
	a_rRecord.flWindMin           = a_rParams.fWindMin;
	a_rRecord.flWindMax           = a_rParams.fWindMax;
	a_rRecord.flMapFlags          = TFLOAT( ( a_rParams.szNormalMap[ 0 ] ? 1 : 0 ) | ( a_rParams.szRoughnessMap[ 0 ] ? 2 : 0 ) | ( a_rParams.szMetallicMap[ 0 ] ? 4 : 0 ) );
	a_rRecord.flUnused            = 0.0f;
}

// Reserved default pages (one per shader) for materials with no XML entry; World and Skin fallbacks differ
static void FillDefaultRecords( TUINT8* a_pPages )
{
	MaterialGPURecord& rWorld  = *TREINTERPRETCAST( MaterialGPURecord*, a_pPages + remaster::MATBUF_DEFAULT_WORLD * MATBUF_PAGE_SIZE );
	rWorld.flReflectivity      = 0.0f;
	rWorld.flFresnelPower      = 0.0f;
	rWorld.flSpecularIntensity = 0.05f;
	rWorld.flSpecularPower     = 26.0f;
	rWorld.flNormalStrength    = 1.0f;
	rWorld.flRoughnessStrength = 1.0f;
	rWorld.flParallaxScale     = 0.0f;
	rWorld.flRoughness         = 0.3f;
	rWorld.flMetallic          = 0.0f;
	rWorld.flEnvSpecIntensity  = 0.0f;
	rWorld.flSpecularF0        = 0.04f;
	rWorld.flEmissiveIntensity = 1.0f;
	rWorld.flWindMin           = 0.0f;
	rWorld.flWindMax           = 1.0f;
	rWorld.flMapFlags          = 0.0f;

	MaterialGPURecord& rSkin  = *TREINTERPRETCAST( MaterialGPURecord*, a_pPages + remaster::MATBUF_DEFAULT_SKIN * MATBUF_PAGE_SIZE );
	rSkin                     = rWorld;
	rSkin.flFresnelPower      = 0.1f;
	rSkin.flSpecularIntensity = 0.0f;
	rSkin.flSpecularPower     = 1.0f;
	rSkin.flRoughness         = 0.0f;
}

static TBOOL BuildMaterialConstantsBuffer()
{
	using namespace remaster;

	T2MUTEX_LOCK_SCOPE( s_oMutex );

	remaster::RenderDX11* pRender = remaster::g_pRender;
	if ( !pRender || !pRender->GetD3D11Device() )
		return TFALSE;

	const TBOOL bHadBuffers = s_pMaterialGPUBuffer || s_ppMaterialPageBuffers;
	ReleaseMaterialGPUBuffers();

	TUINT uiCount = 0;
	for ( auto it = s_oMaterialDB.Begin(); it != s_oMaterialDB.End(); it++ )
		uiCount++;

	const TUINT uiPageCount = 2 + uiCount; // 2 reserved default pages
	TUINT8*     pRecords    = new TUINT8[ uiPageCount * MATBUF_PAGE_SIZE ];
	TUtil::MemClear( pRecords, uiPageCount * MATBUF_PAGE_SIZE );

	FillDefaultRecords( pRecords );

	// Probe that a named map exists (open/close, no decode); a missing file clears the name so
	// the baked presence flag reads "no map" instead of sampling an unbound slot
	auto ValidateMapName = []( TCHAR* a_szName )
	{
		if ( !a_szName[ 0 ] )
			return;

		Toshi::TString8 oPath = TEXTURE_DIR;
		oPath += a_szName;

		if ( Toshi::TFile* pFile = Toshi::TFile::Create( oPath, Toshi::TFILEMODE_READ ) )
			pFile->Destroy();
		else
			a_szName[ 0 ] = '\0';
	};

	TUINT uiPage = 2;
	for ( auto it = s_oMaterialDB.Begin(); it != s_oMaterialDB.End(); it++, uiPage++ )
	{
		MaterialParams& rParams = it->second;

		ValidateMapName( rParams.szNormalMap );
		ValidateMapName( rParams.szRoughnessMap );
		ValidateMapName( rParams.szHeightMap );
		ValidateMapName( rParams.szMetallicMap );

		rParams.uiGPUIndex = TUINT16( uiPage );
		FillMaterialRecord( *TREINTERPRETCAST( MaterialGPURecord*, pRecords + uiPage * MATBUF_PAGE_SIZE ), rParams );
	}

	D3D11_BUFFER_DESC oDesc      = {};
	oDesc.Usage                  = D3D11_USAGE_IMMUTABLE;
	oDesc.BindFlags              = D3D11_BIND_CONSTANT_BUFFER;

	D3D11_SUBRESOURCE_DATA oData = {};

	// Preferred: one buffer, page selected per draw via *SetConstantBuffers1 offsets (needs the
	// 11.1 runtime for >64 KiB); any failure drops to per-page buffers
	if ( pRender->IsCBOffsettingSupported() )
	{
		oDesc.ByteWidth  = uiPageCount * MATBUF_PAGE_SIZE;
		oData.pSysMem    = pRecords;

		if ( FAILED( pRender->GetD3D11Device()->CreateBuffer( &oDesc, &oData, &s_pMaterialGPUBuffer ) ) )
			s_pMaterialGPUBuffer = TNULL;
	}

	if ( !s_pMaterialGPUBuffer )
	{
		s_ppMaterialPageBuffers = new ID3D11Buffer*[ uiPageCount ];
		oDesc.ByteWidth         = MATBUF_PAGE_SIZE;

		for ( TUINT i = 0; i < uiPageCount; i++ )
		{
			oData.pSysMem = pRecords + i * MATBUF_PAGE_SIZE;
			if ( FAILED( pRender->GetD3D11Device()->CreateBuffer( &oDesc, &oData, &s_ppMaterialPageBuffers[ i ] ) ) )
				s_ppMaterialPageBuffers[ i ] = TNULL;
		}
	}

	s_uiMaterialPageCount = uiPageCount;
	s_bMaterialGPUDirty   = TFALSE;
	delete[] pRecords;

	// A rebuild may hand the new buffer the freed old one's address; drop the bind caches so the slot re-binds
	if ( bHadBuffers )
		pRender->InvalidateStateCache();

	return TTRUE;
}

void remaster::PrebuildMaterialConstants()
{
	if ( s_bMaterialGPUDirty )
		BuildMaterialConstantsBuffer();
}

void remaster::BindMaterialConstants( const MaterialParams* a_pParams, TUINT a_uiDefaultPage )
{
	if ( s_bMaterialGPUDirty && !BuildMaterialConstantsBuffer() )
		return;

	const TUINT uiPage = a_pParams ? a_pParams->uiGPUIndex : a_uiDefaultPage;
	TASSERT( uiPage < s_uiMaterialPageCount );

	remaster::RenderDX11* pRender = remaster::g_pRender;
	if ( s_pMaterialGPUBuffer )
	{
		pRender->VSSetConstantBufferRange( 5, s_pMaterialGPUBuffer, uiPage * 16, 16 );
		pRender->PSSetConstantBufferRange( 5, s_pMaterialGPUBuffer, uiPage * 16, 16 );
	}
	else if ( s_ppMaterialPageBuffers && s_ppMaterialPageBuffers[ uiPage ] )
	{
		pRender->VSSetConstantBuffer( 5, s_ppMaterialPageBuffers[ uiPage ] );
		pRender->PSSetConstantBuffer( 5, s_ppMaterialPageBuffers[ uiPage ] );
	}
}

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
	s_bMaterialGPUDirty = TTRUE; // rebuild the b5 material buffer on next bind

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
		oParams.fSpecularF0        = pElem->FloatAttribute( "specularF0", 0.04f );
		// Defaults to specularIntensity capped at 1: raw sun intensities (up to 10) would blow out
		// the HDR cubemap; set envSpecularIntensity explicitly to exceed 1
		const TFLOAT fEnvDefault      = oParams.fSpecularIntensity < 1.0f ? oParams.fSpecularIntensity : 1.0f;
		oParams.fEnvSpecularIntensity = pElem->FloatAttribute( "envSpecularIntensity", fEnvDefault );
		oParams.fMetallic             = pElem->FloatAttribute( "metallic", 0.0f );
		oParams.fNormalStrength       = pElem->FloatAttribute( "normalStrength", 1.0f );
		oParams.fRoughnessStrength    = pElem->FloatAttribute( "roughnessStrength", 1.0f );
		oParams.fParallaxScale        = pElem->FloatAttribute( "parallaxScale", 0.0f );
		oParams.fEmissiveIntensity    = pElem->FloatAttribute( "emissiveIntensity", 1.0f );
		oParams.bWind                 = pElem->BoolAttribute( "wind", false );
		oParams.fWindMin              = pElem->FloatAttribute( "windMin", 0.0f );
		oParams.fWindMax              = pElem->FloatAttribute( "windMax", 1.0f );
		oParams.bFOB                  = pElem->BoolAttribute( "fob", false );

		// Optional normal/roughness/height map file names (resolved against Data\Textures).
		if ( const TCHAR* szNormal = pElem->Attribute( "normalMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szNormalMap, szNormal, sizeof( oParams.szNormalMap ) );
		if ( const TCHAR* szRough = pElem->Attribute( "roughnessMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szRoughnessMap, szRough, sizeof( oParams.szRoughnessMap ) );
		if ( const TCHAR* szHeight = pElem->Attribute( "heightMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szHeightMap, szHeight, sizeof( oParams.szHeightMap ) );
		if ( const TCHAR* szMetal = pElem->Attribute( "metallicMap" ) )
			Toshi::TStringManager::String8Copy( oParams.szMetallicMap, szMetal, sizeof( oParams.szMetallicMap ) );

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

	// Resolve the XML map names into shared cached SRVs. Never rebuild the buffer here: this runs
	// on the loader thread during streaming, and a render-thread rebuild would block on s_oMutex
	// behind the streaming attaches (multi-frame freezes)
	pSlot->pNormalMap    = pSlot->szNormalMap[ 0 ] ? LoadCachedTexture( pSlot->szNormalMap ) : TNULL;
	pSlot->pRoughnessMap = pSlot->szRoughnessMap[ 0 ] ? LoadCachedTexture( pSlot->szRoughnessMap ) : TNULL;
	pSlot->pHeightMap    = pSlot->szHeightMap[ 0 ] ? LoadCachedTexture( pSlot->szHeightMap ) : TNULL;
	pSlot->pMetallicMap  = pSlot->szMetallicMap[ 0 ] ? LoadCachedTexture( pSlot->szMetallicMap ) : TNULL;
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
