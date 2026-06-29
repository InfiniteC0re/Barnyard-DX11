#pragma once
#include <Render/TMaterial.h>
#include <Toshi/T2DList.h>

inline static constexpr TUINT MAX_NUM_ALLOCATED_MATERIALS = 512;
struct MaterialNode : public Toshi::T2DList<MaterialNode>::Node
{
	Toshi::TMaterial* pMaterial;
	TCHAR             szName[ 64 ];
	TUINT16           iNumRefs;
	TUINT16           iId;
	TCHAR             szTextureName[ 32 ];
};

inline static MaterialNode*                 ms_oNodesAlloc    = TREINTERPRETCAST( MaterialNode*, 0x0079b848 );
inline static Toshi::T2DList<MaterialNode>* ms_oFreeMaterials = TREINTERPRETCAST( Toshi::T2DList<MaterialNode>*, 0x007a9878 );
inline static Toshi::T2DList<MaterialNode>* ms_oUsedMaterials = TREINTERPRETCAST( Toshi::T2DList<MaterialNode>*, 0x007b45f0 );

namespace remaster
{

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
	TFLOAT fEmissiveIntensity; // final-color multiplier; >1 pushes into HDR range for bloom (1 = neutral)
	TCHAR  szNormalMap[ 64 ];  // normal-map file under Data\Textures (empty = none)
	TCHAR  szRoughnessMap[ 64 ];
	TCHAR  szHeightMap[ 64 ];  // height map (white = raised) for parallax
	void*  pNormalMap;         // resolved ID3D11ShaderResourceView* (slot copy only)
	void*  pRoughnessMap;
	void*  pHeightMap;
};

// Root folder the XML map names are resolved against. Defined in ERRenderWrapper.cpp.
static constexpr const TCHAR* TEXTURE_DIR = "Data\\Textures\\";

struct MaterialHashComparator
{
	TINT operator()( TUINT32 a, TUINT32 b ) const { return ( a < b ) ? -1 : ( ( a > b ) ? 1 : 0 ); }
};

TUINT32 HashMaterialName( const TCHAR* a_szName );

// Load a texture file (Data\Textures\<name>) into a cached ID3D11ShaderResourceView,
// returning the same view for repeated names. Returns TNULL if the file is missing/bad.
// Defined in ERRenderWrapper.cpp (needs the D3D11 device + DirectXTex).
void* LoadCachedTexture( const TCHAR* a_szName );

// Release all cached map SRVs so the next resolve re-reads the files (hot-reload).
void ClearTextureCache();

void LoadMaterialParamsDB( const TCHAR* a_szPath );
void ReloadMaterialParams();

void ApplyParamsToMaterial( Toshi::TMaterial* a_pMaterial, TUINT32 a_uNameHash );
void AttachMaterialParams( Toshi::TMaterial* a_pMaterial, const TCHAR* a_szName );

} // namespace remaster
