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
	TFLOAT fSpecularPower;     // specular shininess exponent (sun + dynamic-light highlights)
	TFLOAT fSpecularF0;        // GGX/IBL reflectance at normal incidence (0.04 dielectric; higher = more head-on specular)
	TFLOAT fEnvSpecularIntensity; // cube (IBL) reflection strength, independent of SSR; defaults to fSpecularIntensity
	TFLOAT fMetallic;          // 0 = dielectric (old shading), 1 = metal:
	                           // highlights + env reflection tint with the albedo, diffuse softened
	TFLOAT fNormalStrength;    // normal-map perturbation scale (1 = as authored, 0 = flat)
	TFLOAT fRoughnessStrength; // roughness-map multiplier (1 = as authored)
	TFLOAT fParallaxScale;     // parallax occlusion depth (0 = off, ~0.02-0.08 typical)
	TFLOAT fEmissiveIntensity; // final-color multiplier; >1 pushes into HDR range for bloom (1 = neutral)
	TBOOL  bWind;              // enable wind vertex deformation (blue vertex channel = strength)
	TFLOAT fWindMin;           // blue value that maps to 0 wind strength
	TFLOAT fWindMax;           // blue value that maps to full wind strength
	TBOOL  bFOB;               // Wii-style FOB tree billboard: vertex colour blends shadow->lit,
	                           // lit colour comes from the per-instance tint/exposure selector
	TINT8  iShadowAlphaTest;   // alpha-tested shadow casters: -1 = auto (diffuse texture has a
	                           // transparency mask), 0 = force depth-only (no PS), 1 = force on
	TCHAR  szNormalMap[ 64 ];  // normal-map file under Data\Textures (empty = none)
	TCHAR  szRoughnessMap[ 64 ];
	TCHAR  szHeightMap[ 64 ];  // height map (white = raised) for parallax
	TCHAR  szMetallicMap[ 64 ];// metallic map (R channel), scaled by fMetallic
	void*  pNormalMap;         // resolved ID3D11ShaderResourceView* (slot copy only)
	void*  pRoughnessMap;
	void*  pHeightMap;
	void*  pMetallicMap;
	TUINT16 uiGPUIndex;        // 256-byte page index into the static material constants buffer (b5)
};

// Reserved default pages for materials without an XML entry; per-shader defaults differ
inline static constexpr TUINT MATBUF_DEFAULT_WORLD = 0;
inline static constexpr TUINT MATBUF_DEFAULT_SKIN  = 1;

// Bind a material's static constants at b5 (VS+PS) by page; builds the buffer lazily on first
// use after a DB (re)load. TNULL params binds the per-shader default page
void BindMaterialConstants( const MaterialParams* a_pParams, TUINT a_uiDefaultPage );

// Build the material constants buffer up front (from CreateRenderObjects) so the lazy first-use
// build never lands mid-frame or during world streaming
void PrebuildMaterialConstants();

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
