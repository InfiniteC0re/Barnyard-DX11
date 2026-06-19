#pragma once
#include <Render/TRenderContext.h>

#include <d3d11.h>

namespace Toshi { class TRenderPacket; }

// ---------------------------------------------------------------------------
// TRenderPacket::m_pUnk light-ID pack / unpack
//
// m_pUnk is a 32-bit void* that the game never uses.  We repurpose it to
// carry up to 4 TLightID values (one per byte) so a mesh can be lit by
// multiple dynamic glow lights in the same frame.  Invalid slots hold -1.
// ---------------------------------------------------------------------------

inline void* PackRenderPacketLights( const Toshi::TLightIDList& a_rList )
{
	static_assert( sizeof( void* ) == 4 && Toshi::TLightIDList::MAX_NUM_LIGHTS == 4,
	               "Pack assumes 32-bit pointer and 4 light ID slots" );
	TUINT32 packed;
	Toshi::TLightID* p = reinterpret_cast<Toshi::TLightID*>( &packed );
	p[ 0 ] = a_rList.aIDs[ 0 ];
	p[ 1 ] = a_rList.aIDs[ 1 ];
	p[ 2 ] = a_rList.aIDs[ 2 ];
	p[ 3 ] = a_rList.aIDs[ 3 ];
	return reinterpret_cast<void*>( static_cast<uintptr_t>( packed ) );
}

inline void UnpackRenderPacketLights( void* a_pPacked, Toshi::TLightID a_aOut[ Toshi::TLightIDList::MAX_NUM_LIGHTS ] )
{
	const TUINT32          packed = static_cast<TUINT32>( reinterpret_cast<uintptr_t>( a_pPacked ) );
	const Toshi::TLightID* p      = reinterpret_cast<const Toshi::TLightID*>( &packed );
	a_aOut[ 0 ] = p[ 0 ];
	a_aOut[ 1 ] = p[ 1 ];
	a_aOut[ 2 ] = p[ 2 ];
	a_aOut[ 3 ] = p[ 3 ];
}

namespace remaster
{

static constexpr TINT DYNAMIC_GLOW_LIGHT_COUNT       = 4;
static constexpr TINT DYNAMIC_GLOW_SHADOW_RESOLUTION = 512;

// Maximum number of glow light ID slots that can have per-light settings.
// Must be >= the maximum TLightID value assigned by AGlowViewport.
static constexpr TINT MAX_GLOW_LIGHT_SETTINGS = 16;

// Global defaults -- used by any light that has no per-light override registered.
extern TBOOL  g_bDynamicGlowEnabled;
extern TFLOAT g_flDynamicGlowIntensity;
extern TFLOAT g_flDynamicGlowVolumetricIntensity;
extern TFLOAT g_flDynamicGlowColor[ 3 ];
extern TBOOL  g_bDynamicGlowShadowsEnabled;
extern TFLOAT g_flDynamicGlowShadowDistance;
extern TFLOAT g_flDynamicGlowShadowIntensity;
extern TFLOAT g_flDynamicGlowShadowBias;
extern TFLOAT g_flDynamicGlowBumpScale;
extern TBOOL  g_bDynamicGlowFlickerEnabled;
extern TFLOAT g_flDynamicGlowFlickerSpeed;
extern TFLOAT g_flDynamicGlowFlickerStrength;

// Per-light settings.  When bOverride is TFALSE all fields are ignored and the
// global defaults above are used instead (i.e. for game-created lights).
struct GlowLightSettings
{
	TBOOL  bOverride;             // TFALSE -> use global defaults

	TFLOAT flSurfaceIntensity;    // replaces g_flDynamicGlowIntensity
	TFLOAT flVolumetricIntensity; // replaces g_flDynamicGlowVolumetricIntensity
	TFLOAT flColor[ 3 ];          // replaces g_flDynamicGlowColor

	TBOOL  bFlickerEnabled;       // replaces g_bDynamicGlowFlickerEnabled
	TFLOAT flFlickerSpeed;        // replaces g_flDynamicGlowFlickerSpeed
	TFLOAT flFlickerStrength;     // replaces g_flDynamicGlowFlickerStrength

	TFLOAT flShadowIntensity;     // replaces g_flDynamicGlowShadowIntensity
	TFLOAT flShadowBias;          // replaces g_flDynamicGlowShadowBias
	TFLOAT flBumpScale;           // replaces g_flDynamicGlowBumpScale
};

// Array of per-light overrides, indexed by TLightID.
// Zero-initialised at startup -> bOverride = TFALSE for all slots.
extern GlowLightSettings g_aGlowLightSettings[ MAX_GLOW_LIGHT_SETTINGS ];

// Returns the resolved settings for the given light ID.
// If the slot has bOverride == TFALSE the returned struct is populated from the
// global defaults, so callers can always read from the returned value directly.
GlowLightSettings GetGlowLightSettings( Toshi::TLightID a_iLightID );

// Creates the D3D11 constant buffer for the dynamic glow lights cbuffer.
// Caller owns the buffer and must Release() it.
TBOOL CreateDynamicGlowLightsCBuffer( ID3D11Buffer** a_ppBuffer );

TBOOL CreateDynamicGlowShadowResources();
void  DestroyDynamicGlowShadowResources();
void  RenderDynamicGlowShadowMaps();

// Fills the shared dynamic glow lights cbuffer from the render packet's attached
// lights, uploads it, and binds it to PS constant buffer slot 2.
void UploadDynamicGlowLightsCBuffer( Toshi::TRenderPacket* a_pRenderPacket );

// Fills the shared dynamic glow lights cbuffer from the visible glow lights
// collected for shadow rendering this frame, uploads it, and binds it to PS slot 2.
void UploadVolumetricDynamicGlowLightsCBuffer();

} // namespace remaster
