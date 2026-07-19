#pragma once
#include "LightData.h"

#include <Math/TVector4.h>
#include <Math/TMatrix44.h>
#include <Render/TRenderContext.h>
#include <Render/TRenderPacket.h>

#include <d3d11.h>

inline TBOOL RenderPacketHasDynamicLights( Toshi::TRenderPacket* a_pRenderPacket )
{
	return a_pRenderPacket->m_pUnk && TREINTERPRETCAST( LightDataPacket*, a_pRenderPacket->m_pUnk )->oDynamicLights.aIDs[ 0 ] >= 0;
}

namespace remaster
{

static constexpr TINT DYNAMIC_LIGHT_COUNT             = 4;
static constexpr TINT DYNAMIC_LIGHT_SHADOW_RESOLUTION = 512;

static constexpr TINT MAX_DYNAMIC_LIGHT_SETTINGS = 16;

extern TBOOL  g_bDynamicLightEnabled;
extern TFLOAT g_flDynamicLightIntensity;
extern TFLOAT g_flDynamicLightVolumetricIntensity;
extern TFLOAT g_flDynamicLightColor[ 3 ];
extern TBOOL  g_bDynamicLightShadowsEnabled;
extern TFLOAT g_flDynamicLightShadowDistance;
extern TFLOAT g_flDynamicLightShadowIntensity;
extern TFLOAT g_flDynamicLightShadowBias;
extern TFLOAT g_flDynamicLightBumpScale;
extern TBOOL  g_bDynamicLightFlickerEnabled;
extern TFLOAT g_flDynamicLightFlickerSpeed;
extern TFLOAT g_flDynamicLightFlickerStrength;

struct DynamicLightSettings
{
	TBOOL  bOverride;

	TFLOAT flSurfaceIntensity;
	TFLOAT flVolumetricIntensity;
	TFLOAT flColor[ 3 ];

	TBOOL  bFlickerEnabled;
	TFLOAT flFlickerSpeed;
	TFLOAT flFlickerStrength;

	TFLOAT flShadowIntensity;
	TFLOAT flShadowBias;
	TFLOAT flBumpScale;
};

struct DynamicLightCBuffer
{
	Toshi::TVector4  lightPositionRadius[ DYNAMIC_LIGHT_COUNT ]; // xyz = world pos, w = radius
	Toshi::TVector4  lightDirectionCone[ DYNAMIC_LIGHT_COUNT ];  // xyz = direction, w = cosOuter
	Toshi::TMatrix44 matLightVP[ DYNAMIC_LIGHT_COUNT ];
	Toshi::TVector4  shadowParams[ DYNAMIC_LIGHT_COUNT ];        // x = slice, y = texel size, z = bias, w = shadow strength
	Toshi::TVector4  lightColor[ DYNAMIC_LIGHT_COUNT ];          // xyz = RGB, w = volumetric intensity
	Toshi::TVector4  lightIntensity[ DYNAMIC_LIGHT_COUNT ];      // x = surface intensity, y = bump scale, z = cosInner
	Toshi::TVector4  params;                                     // x = count
};

enum StaticLightFlags : TUINT8
{
	STATIC_LIGHT_ENABLED    = 1 << 0,
	STATIC_LIGHT_NIGHT_ONLY = 1 << 1,
};

// Sentinel for StaticPointLight::iLightMag: ignore the day/night cycle, always on (subject only to STATIC_LIGHT_NIGHT_ONLY)
static constexpr TINT8 STATIC_LIGHT_NO_MAG = -1;

// Number of building-light groups in ATerrainInterface::m_afLightMags (mirrors that array's size)
static constexpr TINT STATIC_LIGHT_MAG_COUNT = 12;

struct StaticPointLight
{
	Toshi::TVector4 vPosition; // xyz = world pos, w = radius
	Toshi::TVector4 vColor;    // xyz = RGB,       w = intensity
	TUINT8          uiFlags;   // bitmask of StaticLightFlags
	TINT8           iLightMag; // -1 = always on, else index into ATerrainInterface::m_afLightMags [0..11]:
	                           // the light is active only while that building-light group's mag is > 0
};

static constexpr TINT MAX_STATIC_POINT_LIGHTS = 64;

struct StaticLightCBuffer
{
	Toshi::TVector4 positionRadius[ MAX_STATIC_POINT_LIGHTS ]; // xyz = world pos, w = radius
	Toshi::TVector4 colorIntensity[ MAX_STATIC_POINT_LIGHTS ]; // xyz = RGB,       w = intensity
};

class LightManager
{
public:
	LightManager();
	~LightManager();

	TBOOL Create();
	void  Destroy();

	//-------------------------------------------------------------------------
	// Dynamic lights
	//-------------------------------------------------------------------------

	// Gathers this frame's visible lights and renders a shadow map for each.
	void RenderDynamicLightShadowMaps();

	// Fills the cbuffer from the lights attached to this render packet, uploads it,
	// and binds it to PS slot 2.
	void UploadDynamicLightsCBuffer( Toshi::TRenderPacket* a_pRenderPacket );

	// Same, but sourced from the shadow-casting lights gathered this frame - used by
	// the volumetric fog pass.
	void UploadVolumetricDynamicLightsCBuffer();

	// Resolved settings for a light ID. Slots without an override come back filled from
	// the global defaults, so the caller can always just read the result.
	DynamicLightSettings GetDynamicLightSettings( Toshi::TLightID a_iLightID ) const;

	// Stores a per-light override (bOverride is forced on).
	void SetDynamicLightSettings( Toshi::TLightID a_iLightID, const DynamicLightSettings& a_rSettings );

	// Drops a light's override so it falls back to the global defaults again.
	void ClearDynamicLightSettings( Toshi::TLightID a_iLightID );

	ID3D11ShaderResourceView* GetDynamicLightShadowSRV() const { return m_pShadowSRV; }
	ID3D11SamplerState*       GetDynamicLightShadowSampler() const { return m_pShadowSampler; }

	//-------------------------------------------------------------------------
	// Static point lights (loaded from level data)
	//-------------------------------------------------------------------------

	// Uploads all static lights into the global cbuffer (PS slot 3); call once per frame.
	void UploadStaticLightsGlobalCBuffer();

	// Writes the packet's per-cell static-light indices to b0 (indices 0..3 at a_iVSBaseSlot,
	// count at a_iVSBaseSlot + 1, indices 4..7 at a_iVSIndices2Slot); up to MAX_CELL_STATIC_LIGHTS (8) per cell
	void UploadCellStaticLightIndices( Toshi::TRenderPacket* a_pRenderPacket, TINT a_iVSBaseSlot, TINT a_iVSIndices2Slot );

	// Fills a_pOutIDs (TINT8[MAX_CELL_STATIC_LIGHTS], -1 = empty) with lights whose influence sphere overlaps the bounds
	void GetInfluencingStaticLightIDs( const Toshi::TSphere& a_rcBounds, TINT8* a_pOutIDs ) const;

	// Stores a static light and returns its index, or -1 when the table is full.
	TINT  AddStaticPointLight( const StaticPointLight& a_rLight );
	void  RemoveStaticPointLight( TINT a_iIndex );
	void  ClearStaticPointLights();
	TINT  GetStaticPointLightCount() const { return m_iNumStaticPointLights; }
	StaticPointLight&       GetStaticPointLight( TINT a_iIndex )       { return m_aStaticPointLights[ a_iIndex ]; }
	const StaticPointLight& GetStaticPointLight( TINT a_iIndex ) const { return m_aStaticPointLights[ a_iIndex ]; }

private:
	TBOOL CreateShadowResources();
	TBOOL CreateLightsCBuffer();
	TBOOL CreateStaticLightCBuffer();

	// Where this light sits in the current frame's shadow list, or -1 if it casts none.
	TINT FindShadowIndex( Toshi::TLightID a_iLightID ) const;

	void UploadCBufferData( const DynamicLightCBuffer& a_rCBuffer );

private:
	// Shadow atlas (one slice per light) and the shared light cbuffer.
	ID3D11Buffer*             m_pLightBuffer;
	ID3D11Texture2D*          m_pShadowTexture;
	ID3D11DepthStencilView*   m_apShadowDSV[ DYNAMIC_LIGHT_COUNT ];
	ID3D11ShaderResourceView* m_pShadowSRV;
	ID3D11SamplerState*       m_pShadowSampler;

	// Lights picked to cast a shadow this frame, with their light-view-projection.
	Toshi::TLightID  m_aiShadowLightIDs[ DYNAMIC_LIGHT_COUNT ];
	Toshi::TMatrix44 m_aShadowLightViewProj[ DYNAMIC_LIGHT_COUNT ];
	TINT             m_iNumShadowLights;

	// Last uploaded contents, so an unchanged cbuffer doesn't get re-mapped.
	DynamicLightCBuffer m_oPreviousBuffer;
	TBOOL               m_bPreviousBufferValid;

	// Per-light overrides indexed by TLightID; a fresh slot has bOverride off.
	DynamicLightSettings m_aDynamicLightSettings[ MAX_DYNAMIC_LIGHT_SETTINGS ];

	// Fixed lights loaded from the level, and the global cbuffer holding their data.
	StaticPointLight m_aStaticPointLights[ MAX_STATIC_POINT_LIGHTS ];
	TINT             m_iNumStaticPointLights;
	ID3D11Buffer*    m_pStaticLightBuffer;
};

extern LightManager* g_pLightManager;

} // namespace remaster
