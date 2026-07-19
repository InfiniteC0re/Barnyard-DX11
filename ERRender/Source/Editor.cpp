#include "pch.h"
#include "Editor.h"
#include "Settings.h"
#include "LightManager.h"
#include "CubemapAnchors.h"
#include "StaticLightsFile.h"
#include "RenderParams.h"

#include <AHooks.h>
#include <AImGui.h>
#include <HookHelpers.h>

#include <Input/TInputDeviceKeyboard.h>
#include <Math/TMatrix44.h>
#include <Render/TRenderContext.h>

#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/AGUI2.h>
#include <BYardSDK/ARootTask.h>
#include <BYardSDK/AGlowViewport.h>
#include <BYardSDK/ACamera.h>

#include <imgui.h>
#include "ImGuizmo/ImGuizmo.h"
#include "RenderDX11.h"
#include "RenderContentDX11.h"

#include <BYardSDK/ARenderer.h>

#include <ToshiTools/tinyxml2.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

using namespace remaster;

static constexpr TINT KAWASE_MAX_LEVELS = 5;

namespace editor
{

TBOOL g_bEnabled = TFALSE;

struct EditorLight
{
	TCHAR  szName[ 64 ];
	TFLOAT vPosition[ 3 ]; // world-space XYZ
	TFLOAT flAzimuth;      // horizontal angle, degrees: 0=+Z, 90=+X
	TFLOAT flElevation;    // vertical angle, degrees: +90=straight up, -90=straight down
	TFLOAT flFOV;          // full cone angle, degrees
	TFLOAT flRange;        // far clip / influence radius

	TBOOL bEnabled;
	TBOOL bNightOnly;

	// Per-light rendering settings -- registered with the LightManager each frame
	DynamicLightSettings settings;

	AGlowViewport::GlowObject* pGlowObject;
};

static constexpr TINT MAX_EDITOR_LIGHTS = remaster::DYNAMIC_LIGHT_COUNT;
static EditorLight    s_aLights[ MAX_EDITOR_LIGHTS ];
static TINT           s_iNumLights = 0;

// Builds a world-space TMatrix44 with FORWARD pointing along the given azimuth/elevation
//   azimuth  : degrees, clockwise from +Z looking down (0=+Z, 90=+X)
//   elevation: degrees, +90=straight up, -90=straight down
static TMatrix44 BuildLightTransform( const TFLOAT pos[ 3 ], TFLOAT azimuthDeg, TFLOAT elevationDeg )
{
	const TFLOAT az = TMath::DegToRad( azimuthDeg );
	const TFLOAT el = TMath::DegToRad( elevationDeg );

	TVector4 forward(
	    TMath::Cos( el ) * TMath::Sin( az ),
	    TMath::Sin( el ),
	    TMath::Cos( el ) * TMath::Cos( az ),
	    0.0f
	);

	// Choose a stable world-up reference; fall back to +Z when forward is nearly vertical
	TVector4 worldUp = ( TMath::Abs( forward.y ) > 0.99f ) ? TVector4( 0.0f, 0.0f, 1.0f, 0.0f ) : TVector4( 0.0f, 1.0f, 0.0f, 0.0f );

	// right = worldUp x forward
	TVector4 right(
	    worldUp.y * forward.z - worldUp.z * forward.y,
	    worldUp.z * forward.x - worldUp.x * forward.z,
	    worldUp.x * forward.y - worldUp.y * forward.x,
	    0.0f
	);
	right.Normalise();

	// up = forward x right
	TVector4 up(
	    forward.y * right.z - forward.z * right.y,
	    forward.z * right.x - forward.x * right.z,
	    forward.x * right.y - forward.y * right.x,
	    0.0f
	);
	up.Normalise();

	TMatrix44 mat;
	mat.AsBasisVector4( BASISVECTOR_RIGHT )       = right;
	mat.AsBasisVector4( BASISVECTOR_UP )          = up;
	mat.AsBasisVector4( BASISVECTOR_FORWARD )     = forward;
	mat.AsBasisVector4( BASISVECTOR_TRANSLATION ) = TVector4( pos[ 0 ], pos[ 1 ], pos[ 2 ], 1.0f );
	return mat;
}

// Computes viewport + projection params that produce a cone with the given full FOV angle
// Uses a 2x2 virtual viewport so that (fWidth * 0.25 = 0.5) and:
//   tan(halfFOV) = 0.5 / m_Proj.x  ->  m_Proj.x = 0.5 / tan(halfFOV)
static void BuildLightProjection(
    TRenderContext::VIEWPORTPARAMS&   a_rVP,
    TRenderContext::PROJECTIONPARAMS& a_rPP,
    TFLOAT                            a_flFOVDeg,
    TFLOAT                            a_flRange
)
{
	a_rVP.fX      = 0.0f;
	a_rVP.fY      = 0.0f;
	a_rVP.fWidth  = 2.0f;
	a_rVP.fHeight = 2.0f;
	a_rVP.fMinZ   = 0.0f;
	a_rVP.fMaxZ   = 1.0f;

	const TFLOAT fHalfFOV = TMath::DegToRad( a_flFOVDeg ) * 0.5f;

	a_rPP.m_Proj.x = a_rPP.m_Proj.y = 0.5f / TMath::Max( TMath::Tan( fHalfFOV ), 0.001f );
	a_rPP.m_Centre.x = a_rPP.m_Centre.y = 1.0f;
	a_rPP.m_fNearClip                   = 0.1f;
	a_rPP.m_fFarClip                    = TMath::Max( a_flRange, 1.0f );
}

static void ApplyLightToGlowObject( EditorLight& a_rLight )
{
	if ( !a_rLight.pGlowObject )
		return;

	TMatrix44 oTransform = BuildLightTransform( a_rLight.vPosition, a_rLight.flAzimuth, a_rLight.flElevation );

	TRenderContext::VIEWPORTPARAMS   vp;
	TRenderContext::PROJECTIONPARAMS pp;
	BuildLightProjection( vp, pp, a_rLight.flFOV, a_rLight.flRange );

	// Setup() internally inverts the matrix to transform frustum planes to world space,
	// so it expects the view matrix (world-to-light-space), not the world transform
	TMatrix44 oViewMatrix;
	oViewMatrix.InvertOrthogonal( oTransform );

	a_rLight.pGlowObject->Setup( oViewMatrix, vp, pp, TRenderContext::CameraMode_Perspective );

	// Store the world transform separately -- BuildGlowObjectWorldTransform reads m_oTransform
	// directly for editor lights (those with m_pSceneObject == TNULL)
	a_rLight.pGlowObject->m_oTransform    = oTransform;
	a_rLight.pGlowObject->m_pSceneObject  = TNULL;
	a_rLight.pGlowObject->m_bEnabled      = a_rLight.bEnabled;
	a_rLight.pGlowObject->m_bIsNightLight = a_rLight.bNightOnly;

	// Register per-light rendering settings so the upload path uses them instead of the global defaults
	const Toshi::TLightID iID = a_rLight.pGlowObject->m_iID;
	if ( g_pLightManager && iID >= 0 && iID < MAX_DYNAMIC_LIGHT_SETTINGS )
	{
		a_rLight.settings.bOverride = TTRUE;
		g_pLightManager->SetDynamicLightSettings( iID, a_rLight.settings );
	}
}

static void AddLight()
{
	if ( s_iNumLights >= MAX_EDITOR_LIGHTS )
		return;

	AGlowViewport* pVP = AGlowViewport::GetSingleton();
	if ( !pVP )
		return;

	AGlowViewport::GlowObject* pObj = pVP->CreateGlowObject();
	if ( !pObj )
		return;

	EditorLight& light = s_aLights[ s_iNumLights ];
	TUtil::MemClear( &light, sizeof( light ) );

	_snprintf_s( light.szName, sizeof( light.szName ), _TRUNCATE, "Light %d", s_iNumLights + 1 );
	light.pGlowObject = pObj;
	light.flAzimuth   = 0.0f;
	light.flElevation = -90.0f; // point straight down by default
	light.flFOV       = 60.0f;
	light.flRange     = 20.0f;
	light.bEnabled    = TTRUE;
	light.bNightOnly  = TFALSE;

	light.settings           = g_pLightManager ? g_pLightManager->GetDynamicLightSettings( -1 ) // -1 always returns defaults
	                                             :
	                                             DynamicLightSettings{};
	light.settings.bOverride = TTRUE;

	ApplyLightToGlowObject( light );
	s_iNumLights++;
}

static void RemoveLight( TINT a_iIndex )
{
	if ( a_iIndex < 0 || a_iIndex >= s_iNumLights )
		return;

	AGlowViewport* pVP = AGlowViewport::GetSingleton();

	if ( s_aLights[ a_iIndex ].pGlowObject )
	{
		// Clear per-light settings so the slot reverts to global defaults
		const Toshi::TLightID iID = s_aLights[ a_iIndex ].pGlowObject->m_iID;
		if ( g_pLightManager )
			g_pLightManager->ClearDynamicLightSettings( iID );

		if ( pVP )
			pVP->RemoveGlowObject( s_aLights[ a_iIndex ].pGlowObject );
	}

	// Shift remaining entries down
	for ( TINT i = a_iIndex; i < s_iNumLights - 1; i++ )
		s_aLights[ i ] = s_aLights[ i + 1 ];

	s_iNumLights--;
	TUtil::MemClear( &s_aLights[ s_iNumLights ], sizeof( EditorLight ) );
}

// True while this module is holding an ImGui input lock for a debug overlay
static TBOOL s_bDebugInputLocked = TFALSE;

// AImGUI's lock is a counter (m_iNumInputLocks); toggling LockInput on every open/close would
// let it drift above zero and never release, so track our own held state to keep it balanced
static void SyncDebugInputLock()
{
	const TBOOL bWantLock = g_bEnabled || settings::g_bEnabled;
	if ( bWantLock == s_bDebugInputLocked )
		return;

	if ( bWantLock )
		g_pImGui->LockInput();
	else
		g_pImGui->UnlockInput();

	s_bDebugInputLocked = bWantLock;
}

MEMBER_HOOK( 0x004293d0, AGameStateController, AGameStateController_ProcessInput, TBOOL, TInputInterface::InputEvent* a_pInputEvent )
{
	if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN )
	{
		Toshi::TInputDeviceKeyboard* pKeyboard = TSTATICCAST( Toshi::TInputDeviceKeyboard, a_pInputEvent->GetSource() );

		if ( pKeyboard->IsAltDown() && a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_Z )
		{
			g_bEnabled = !g_bEnabled;
			if ( g_bEnabled ) settings::g_bEnabled = TFALSE; // only one debug overlay open at a time

			SyncDebugInputLock();
			return TTRUE;
		}

		if ( pKeyboard->IsAltDown() && a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_G )
		{
			settings::g_bEnabled = !settings::g_bEnabled;
			if ( settings::g_bEnabled ) g_bEnabled = TFALSE; // only one debug overlay open at a time

			SyncDebugInputLock();
			return TTRUE;
		}
	}

	return CallOriginal( a_pInputEvent );
}

void         OnGameFXChanged( const TCHAR* a_szFXName, TBOOL a_bForce );
const TCHAR* FXSettings_GetCurrentName();
static TBOOL LevelNamesEqual( const TCHAR* a_szLhs, const TCHAR* a_szRhs );
static void  FXSettings_InvalidateCache();
static void  LevelSettings_ResetToDefaults();

static TCHAR s_szFXSlotsDebug[ 256 ] = "";

static const TCHAR* GameFXNameAt( TUINTPTR a_pFX )
{
	const TCHAR* szName = *TREINTERPRETCAST( const TCHAR**, a_pFX + 0x114 );
	if ( !szName || szName[ 0 ] < 0x20 || szName[ 0 ] > 0x7E )
		return TNULL;
	return szName;
}

class AGameTimeFXManager
{};

MEMBER_HOOK( 0x00545630, AGameTimeFXManager, AGameTimeFXManager_OnUpdate, TBOOL, TFLOAT a_fDeltaTime )
{
	const TBOOL bResult = CallOriginal( a_fDeltaTime );
	FXSettings_Update( a_fDeltaTime );
	return bResult;
}

void SetupHooks()
{
	InstallHook<AGameStateController_ProcessInput>();
	InstallHook<AGameTimeFXManager_OnUpdate>();
}

static TINT                s_iSelectedLight       = -1; // selected dynamic light, -1 = none
static TINT                s_iSelectedStaticLight = -1; // selected static light, -1 = none
static TINT                s_iSelectedAnchor      = -1; // selected cubemap anchor, -1 = none
static ImGuizmo::OPERATION s_eGizmoOp             = ImGuizmo::TRANSLATE;

static TBOOL LevelNamesEqual( const TCHAR* a_szLhs, const TCHAR* a_szRhs )
{
	return a_szLhs && a_szRhs && T2String8::CompareNoCase( a_szLhs, a_szRhs ) == 0;
}

void DynamicLights_LoadForCurrentLevel( const TCHAR* a_szPath, TBOOL a_bGlowObjectsInvalid )
{
	// Glow objects outlive a level change (persistent viewport singleton) and must be removed
	// properly or they leak and later alias live scene memory (render crash); only when the
	// singleton itself is gone can we detach without dereferencing the stale pointers
	if ( a_bGlowObjectsInvalid && !AGlowViewport::GetSingleton() )
	{
		if ( g_pLightManager )
			for ( TINT i = 0; i < MAX_DYNAMIC_LIGHT_SETTINGS; i++ )
				g_pLightManager->ClearDynamicLightSettings( i );
		TUtil::MemClear( s_aLights, sizeof( s_aLights ) );
		s_iNumLights = 0;
	}
	else
	{
		while ( s_iNumLights > 0 )
			RemoveLight( s_iNumLights - 1 );
	}
	s_iSelectedLight = -1;

	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 )
		return;
	const TCHAR* szLevelName = CubemapAnchors_GetLevelName( iLevel );

	tinyxml2::XMLDocument oDoc;
	if ( oDoc.LoadFile( a_szPath ) != tinyxml2::XML_SUCCESS )
		return;

	const tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "dynamicLights" );
	if ( !pRoot )
		return;

	for ( const tinyxml2::XMLElement* pLevel = pRoot->FirstChildElement( "level" );
	      pLevel;
	      pLevel = pLevel->NextSiblingElement( "level" ) )
	{
		const TCHAR* szName = pLevel->Attribute( "name" );
		if ( !szName || !LevelNamesEqual( szName, szLevelName ) )
			continue;

		for ( const tinyxml2::XMLElement* pElem = pLevel->FirstChildElement( "light" );
		      pElem;
		      pElem = pElem->NextSiblingElement( "light" ) )
		{
			if ( s_iNumLights >= MAX_EDITOR_LIGHTS )
				break;

			// AddLight seeds global defaults, so attributes missing from older files keep sane values
			const TINT iPrev = s_iNumLights;
			AddLight();
			if ( s_iNumLights == iPrev )
				break;

			EditorLight& light = s_aLights[ s_iNumLights - 1 ];

			if ( const TCHAR* szLightName = pElem->Attribute( "name" ) )
				T2String8::CopySafe( light.szName, szLightName, sizeof( light.szName ) );

			light.vPosition[ 0 ] = pElem->FloatAttribute( "x", 0.0f );
			light.vPosition[ 1 ] = pElem->FloatAttribute( "y", 0.0f );
			light.vPosition[ 2 ] = pElem->FloatAttribute( "z", 0.0f );
			light.flAzimuth      = pElem->FloatAttribute( "azimuth", 0.0f );
			light.flElevation    = pElem->FloatAttribute( "elevation", -90.0f );
			light.flFOV          = pElem->FloatAttribute( "fov", 60.0f );
			light.flRange        = pElem->FloatAttribute( "range", 20.0f );
			light.bEnabled       = pElem->BoolAttribute( "enabled", true );
			light.bNightOnly     = pElem->BoolAttribute( "nightOnly", false );

			DynamicLightSettings& s = light.settings;
			s.flSurfaceIntensity    = pElem->FloatAttribute( "surfaceIntensity", s.flSurfaceIntensity );
			s.flVolumetricIntensity = pElem->FloatAttribute( "volumetricIntensity", s.flVolumetricIntensity );
			s.flColor[ 0 ]          = pElem->FloatAttribute( "r", s.flColor[ 0 ] );
			s.flColor[ 1 ]          = pElem->FloatAttribute( "g", s.flColor[ 1 ] );
			s.flColor[ 2 ]          = pElem->FloatAttribute( "b", s.flColor[ 2 ] );
			s.bFlickerEnabled       = pElem->BoolAttribute( "flicker", s.bFlickerEnabled != TFALSE );
			s.flFlickerSpeed        = pElem->FloatAttribute( "flickerSpeed", s.flFlickerSpeed );
			s.flFlickerStrength     = pElem->FloatAttribute( "flickerStrength", s.flFlickerStrength );
			s.flShadowIntensity     = pElem->FloatAttribute( "shadowIntensity", s.flShadowIntensity );
			s.flShadowBias          = pElem->FloatAttribute( "shadowBias", s.flShadowBias );
			s.flBumpScale           = pElem->FloatAttribute( "bumpScale", s.flBumpScale );
			s.bOverride             = TTRUE;

			ApplyLightToGlowObject( light );
		}

		break;
	}
}

void DynamicLights_SaveCurrentLevel( const TCHAR* a_szPath )
{
	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 )
		return;
	const TCHAR* szLevelName = CubemapAnchors_GetLevelName( iLevel );

	// Load the existing file first so other levels' lights survive the write
	tinyxml2::XMLDocument oDoc;
	oDoc.LoadFile( a_szPath ); // missing file just means a fresh document

	tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "dynamicLights" );
	if ( !pRoot )
	{
		pRoot = oDoc.NewElement( "dynamicLights" );
		oDoc.InsertFirstChild( pRoot );
	}

	for ( tinyxml2::XMLElement* pLevel = pRoot->FirstChildElement( "level" ); pLevel; )
	{
		tinyxml2::XMLElement* pNext  = pLevel->NextSiblingElement( "level" );
		const TCHAR*          szName = pLevel->Attribute( "name" );
		if ( szName && LevelNamesEqual( szName, szLevelName ) )
			pRoot->DeleteChild( pLevel );
		pLevel = pNext;
	}

	// Only write a <level> element when the level has lights, so an emptied level leaves no stray entry
	if ( s_iNumLights > 0 )
	{
		tinyxml2::XMLElement* pLevel = oDoc.NewElement( "level" );
		pLevel->SetAttribute( "name", szLevelName );

		for ( TINT i = 0; i < s_iNumLights; i++ )
		{
			const EditorLight&    rLight = s_aLights[ i ];
			tinyxml2::XMLElement* pElem  = oDoc.NewElement( "light" );
			pElem->SetAttribute( "name", rLight.szName );
			pElem->SetAttribute( "x", rLight.vPosition[ 0 ] );
			pElem->SetAttribute( "y", rLight.vPosition[ 1 ] );
			pElem->SetAttribute( "z", rLight.vPosition[ 2 ] );
			pElem->SetAttribute( "azimuth", rLight.flAzimuth );
			pElem->SetAttribute( "elevation", rLight.flElevation );
			pElem->SetAttribute( "fov", rLight.flFOV );
			pElem->SetAttribute( "range", rLight.flRange );
			pElem->SetAttribute( "enabled", rLight.bEnabled != TFALSE );
			pElem->SetAttribute( "nightOnly", rLight.bNightOnly != TFALSE );

			const DynamicLightSettings& s = rLight.settings;
			pElem->SetAttribute( "surfaceIntensity", s.flSurfaceIntensity );
			pElem->SetAttribute( "volumetricIntensity", s.flVolumetricIntensity );
			pElem->SetAttribute( "r", s.flColor[ 0 ] );
			pElem->SetAttribute( "g", s.flColor[ 1 ] );
			pElem->SetAttribute( "b", s.flColor[ 2 ] );
			pElem->SetAttribute( "flicker", s.bFlickerEnabled != TFALSE );
			pElem->SetAttribute( "flickerSpeed", s.flFlickerSpeed );
			pElem->SetAttribute( "flickerStrength", s.flFlickerStrength );
			pElem->SetAttribute( "shadowIntensity", s.flShadowIntensity );
			pElem->SetAttribute( "shadowBias", s.flShadowBias );
			pElem->SetAttribute( "bumpScale", s.flBumpScale );
			pLevel->InsertEndChild( pElem );
		}

		pRoot->InsertEndChild( pLevel );
	}

	oDoc.SaveFile( a_szPath );
}

// Per-level render settings (Data\LevelSettings.xml). Debug-only toggles are deliberately
// left out of s_aLevelSettings, so they are not persisted
struct LevelSetting
{
	const TCHAR* szName; // XML attribute name (arrays: name0..nameN-1)
	enum Type : TUINT8
	{
		T_BOOL,
		T_INT,
		T_FLOAT
	} eType;
	void* pValue;
	TINT  iCount;
};

#define LS_BOOL( name, var )      { name, LevelSetting::T_BOOL, &var, 1 }
#define LS_INT( name, var )       { name, LevelSetting::T_INT, &var, 1 }
#define LS_FLOAT( name, var )     { name, LevelSetting::T_FLOAT, &var, 1 }
#define LS_FLOATN( name, var, n ) { name, LevelSetting::T_FLOAT, var, n }

static const LevelSetting s_aLevelSettings[] = {
	// Sun
	LS_BOOL( "overrideSunDirection", remaster::g_bOverrideSunDirection ),
	LS_FLOAT( "sunAzimuth", remaster::g_flSunAzimuth ),
	LS_FLOAT( "sunElevation", remaster::g_flSunElevation ),
	// CSM
	LS_BOOL( "csmEnabled", remaster::g_bCSMEnabled ),
	LS_BOOL( "csmDelayedCascadeUpdate", remaster::g_bCSMDelayedCascadeUpdate ),
	LS_FLOAT( "shadowIntensity", remaster::g_flShadowIntensity ),
	LS_FLOAT( "shadowDistance", remaster::g_flShadowDistance ),
	LS_FLOAT( "shadowSplitLambda", remaster::g_flShadowSplitLambda ),
	LS_FLOAT( "shadowCascadeBlend", remaster::g_flShadowCascadeBlend ),
	LS_FLOAT( "shadowMinSlopeBias", remaster::g_flShadowMinSlopeScaledDepthBias ),
	LS_FLOAT( "shadowReceiverPlaneBias", remaster::g_flShadowReceiverPlaneBias ),
	LS_FLOAT( "shadowNormalOffset", remaster::g_flShadowNormalOffsetScale ),
	LS_FLOAT( "shadowGrazingScale", remaster::g_flShadowGrazingScale ),
	LS_FLOATN( "cascadePadding", remaster::g_aflShadowCascadePadding, remaster::CSM_CASCADE_COUNT ),
	LS_FLOATN( "casterPadding", remaster::g_aflShadowCasterPadding, remaster::CSM_CASCADE_COUNT ),
	LS_FLOATN( "cascadeSlopeBias", remaster::g_aflShadowSlopeScaledDepthBias, remaster::CSM_CASCADE_COUNT ),
	LS_FLOATN( "cascadeReceiverBias", remaster::g_aflShadowReceiverBias, remaster::CSM_CASCADE_COUNT ),
	LS_FLOATN( "cascadePCFRadius", remaster::g_aflShadowPCFRadius, remaster::CSM_CASCADE_COUNT ),
	// Cloud shadows
	LS_BOOL( "cloudShadows", remaster::g_bCloudShadowsEnabled ),
	LS_BOOL( "cloudVolumetrics", remaster::g_bCloudShadowsVolumetrics ),
	LS_FLOAT( "cloudStrength", remaster::g_flCloudShadowStrength ),
	LS_FLOAT( "cloudRegionSize", remaster::g_flCloudShadowRegionSize ),
	LS_FLOAT( "cloudFeatureScale", remaster::g_flCloudShadowFeatureScale ),
	LS_FLOAT( "cloudCoverage", remaster::g_flCloudShadowCoverage ),
	LS_FLOAT( "cloudDensity", remaster::g_flCloudShadowDensity ),
	LS_FLOAT( "cloudContrast", remaster::g_flCloudShadowContrast ),
	LS_FLOAT( "cloudSpeed", remaster::g_flCloudShadowSpeed ),
	LS_FLOATN( "cloudWindDir", remaster::g_flCloudShadowWindDir, 2 ),
	// Ambient occlusion
	LS_BOOL( "aoEnabled", remaster::g_bHBAOEnabled ),
	LS_INT( "aoAlgorithm", remaster::g_iAOAlgorithm ),
	LS_FLOAT( "aoRadius", remaster::g_flHBAORadius ),
	LS_FLOAT( "aoSceneScale", remaster::g_flHBAOSceneScale ),
	LS_FLOAT( "aoIntensity", remaster::g_flHBAOIntensity ),
	LS_FLOAT( "aoPower", remaster::g_flHBAOPower ),
	LS_FLOAT( "aoBlurSharpness", remaster::g_flHBAOBlurSharpness ),
	LS_FLOAT( "aoBias", remaster::g_flHBAOBias ),
	LS_FLOAT( "xeRadiusMultiplier", remaster::g_flXeGTAORadiusMultiplier ),
	LS_FLOAT( "xeFalloffRange", remaster::g_flXeGTAOFalloffRange ),
	LS_FLOAT( "xeDistribution", remaster::g_flXeGTAOSampleDistributionPower ),
	LS_FLOAT( "xeThinOccluder", remaster::g_flXeGTAOThinOccluderCompensation ),
	// SSR
	LS_BOOL( "ssrEnabled", remaster::g_bSSREnabled ),
	LS_FLOAT( "ssrIntensity", remaster::g_flSSRIntensity ),
	LS_FLOAT( "ssrMaxDistance", remaster::g_flSSRMaxDistance ),
	LS_INT( "ssrMaxSteps", remaster::g_iSSRMaxSteps ),
	LS_FLOAT( "ssrStride", remaster::g_flSSRStepSize ),
	LS_FLOAT( "ssrThickness", remaster::g_flSSRThickness ),
	LS_FLOAT( "ssrFresnelPower", remaster::g_flSSRFresnelPower ),
	LS_FLOAT( "ssrEdgeFade", remaster::g_flSSREdgeFade ),
	// Reflection cube / IBL
	LS_BOOL( "skyCubeEnabled", remaster::g_bSkyCubeEnabled ),
	LS_BOOL( "reflectTerrain", remaster::g_bReflectTerrain ),
	LS_FLOAT( "skyCubeIntensity", remaster::g_flSkyCubeIntensity ),
	LS_BOOL( "envSpecular", remaster::g_bEnvSpecular ),
	LS_FLOAT( "cubeBlendTime", remaster::g_flSkyCubeBlendTime ),
	LS_FLOAT( "cubeRefreshTime", remaster::g_flSkyCubeRefreshTime ),
	LS_FLOAT( "cubeParallaxH", remaster::g_flSkyCubeParallaxHorizontal ),
	LS_FLOAT( "cubeParallaxV", remaster::g_flSkyCubeParallaxVertical ),
	// Wind
	LS_BOOL( "windEnabled", remaster::g_bWindEnabled ),
	LS_FLOAT( "windStrength", remaster::g_flWindStrength ),
	LS_FLOAT( "windSpeed", remaster::g_flWindSpeed ),
	LS_FLOATN( "windDir", remaster::g_flWindDir, 2 ),
	// Glow bloom
	LS_BOOL( "glowBloomEnabled", remaster::g_bGlowBloomEnabled ),
	LS_INT( "glowBloomLevels", remaster::g_iGlowBloomKawaseLevels ),
	LS_FLOAT( "glowBloomOffset", remaster::g_flGlowBloomKawaseOffset ),
	LS_FLOAT( "glowBloomIntensity", remaster::g_flGlowBloomIntensity ),
	// HDR bloom
	LS_BOOL( "hdrBloomEnabled", remaster::g_bHDRBloomEnabled ),
	LS_INT( "hdrBloomLevels", remaster::g_iHDRBloomKawaseLevels ),
	LS_FLOAT( "hdrBloomOffset", remaster::g_flHDRBloomKawaseOffset ),
	LS_FLOAT( "hdrBloomThreshold", remaster::g_flHDRBloomThreshold ),
	LS_FLOAT( "hdrBloomIntensity", remaster::g_flHDRBloomIntensity ),
	// Dynamic light defaults
	LS_BOOL( "dynLightsEnabled", remaster::g_bDynamicLightEnabled ),
	LS_FLOAT( "dynLightIntensity", remaster::g_flDynamicLightIntensity ),
	LS_FLOAT( "dynLightVolumetric", remaster::g_flDynamicLightVolumetricIntensity ),
	LS_FLOAT( "dynLightBumpScale", remaster::g_flDynamicLightBumpScale ),
	LS_FLOATN( "dynLightColor", remaster::g_flDynamicLightColor, 3 ),
	LS_BOOL( "dynShadowsEnabled", remaster::g_bDynamicLightShadowsEnabled ),
	LS_FLOAT( "dynShadowDistance", remaster::g_flDynamicLightShadowDistance ),
	LS_FLOAT( "dynShadowIntensity", remaster::g_flDynamicLightShadowIntensity ),
	LS_FLOAT( "dynShadowBias", remaster::g_flDynamicLightShadowBias ),
	LS_BOOL( "dynFlicker", remaster::g_bDynamicLightFlickerEnabled ),
	LS_FLOAT( "dynFlickerSpeed", remaster::g_flDynamicLightFlickerSpeed ),
	LS_FLOAT( "dynFlickerStrength", remaster::g_flDynamicLightFlickerStrength ),
};

// Stored per game FX preset name (<fx> blocks) rather than per level, and interpolated when the FX switches
static const LevelSetting s_aFXSettings[] = {
	// Sun shafts
	LS_BOOL( "sunShaftsEnabled", remaster::g_bSunShaftsEnabled ),
	LS_FLOAT( "sunShaftsIntensity", remaster::g_flSunShaftsAlpha ),
	LS_FLOAT( "sunShaftsRaysLength", remaster::g_flSunShaftsRaysLength ),
	LS_FLOATN( "sunShaftsTint", remaster::g_flSunShaftsTint, 3 ),
	LS_INT( "sunShaftsLevels", remaster::g_iSunShaftsKawaseLevels ),
	LS_FLOAT( "sunShaftsOffset", remaster::g_flSunShaftsKawaseOffset ),
	// Volumetric fog
	LS_BOOL( "volFogEnabled", remaster::g_bVolumetricFogEnabled ),
	LS_BOOL( "volFogUseSceneColor", remaster::g_bVolumetricFogUseSceneColor ),
	LS_INT( "volFogComposite", remaster::g_iVolumetricFogCompositeMode ),
	LS_FLOAT( "volFogDensity", remaster::g_flVolumetricFogDensity ),
	LS_FLOAT( "volFogG", remaster::g_flVolumetricFogG ),
	LS_FLOAT( "volFogMaxDist", remaster::g_flVolumetricFogMaxDist ),
	LS_FLOAT( "volFogIntensity", remaster::g_flVolumetricFogIntensity ),
	LS_FLOATN( "volFogColor", remaster::g_flVolumetricFogColor, 3 ),
	LS_FLOAT( "volFogBottom", remaster::g_flVolumetricFogHeight ),
	LS_FLOAT( "volFogTop", remaster::g_flVolumetricFogTopHeight ),
	LS_FLOAT( "volFogNoiseStrength", remaster::g_flVolumetricFogNoiseStrength ),
	LS_FLOAT( "volFogNoiseScale", remaster::g_flVolumetricFogNoiseScale ),
	LS_FLOATN( "volFogWindDir", remaster::g_flVolumetricFogWindDir, 2 ),
	LS_FLOAT( "volFogWindSpeed", remaster::g_flVolumetricFogWindSpeed ),
};

#undef LS_BOOL
#undef LS_INT
#undef LS_FLOAT
#undef LS_FLOATN

// Builds "name" for scalars, "name<i>" for array elements
static void LevelSettingAttrName( const LevelSetting& a_rSetting, TINT a_iElem, TCHAR* a_pOut, TSIZE a_uiOutSize )
{
	if ( a_rSetting.iCount > 1 )
		T2String8::Format( a_pOut, TINT( a_uiOutSize ), "%s%d", a_rSetting.szName, a_iElem );
	else
		T2String8::CopySafe( a_pOut, a_rSetting.szName, a_uiOutSize );
}

void LevelSettings_ApplyForCurrentLevel( const TCHAR* a_szPath )
{
	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 )
		return;
	const TCHAR* szLevelName = CubemapAnchors_GetLevelName( iLevel );

	// Start from defaults so a setting this level omits resets rather than leaking the previous level's value
	LevelSettings_ResetToDefaults();

	tinyxml2::XMLDocument oDoc;
	if ( oDoc.LoadFile( a_szPath ) != tinyxml2::XML_SUCCESS )
		return;

	const tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "levelSettings" );
	if ( !pRoot )
		return;

	for ( const tinyxml2::XMLElement* pLevel = pRoot->FirstChildElement( "level" );
	      pLevel;
	      pLevel = pLevel->NextSiblingElement( "level" ) )
	{
		const TCHAR* szName = pLevel->Attribute( "name" );
		if ( !szName || !LevelNamesEqual( szName, szLevelName ) )
			continue;

		for ( const LevelSetting& rSetting : s_aLevelSettings )
		{
			for ( TINT iElem = 0; iElem < rSetting.iCount; iElem++ )
			{
				TCHAR szAttr[ 96 ];
				LevelSettingAttrName( rSetting, iElem, szAttr, sizeof( szAttr ) );

				switch ( rSetting.eType )
				{
					case LevelSetting::T_BOOL:
					{
						TBOOL bValue;
						if ( pLevel->QueryBoolAttribute( szAttr, &bValue ) == tinyxml2::XML_SUCCESS )
							*(TBOOL*)rSetting.pValue = bValue ? TTRUE : TFALSE;
						break;
					}
					case LevelSetting::T_INT:
					{
						TINT iValue;
						if ( pLevel->QueryIntAttribute( szAttr, &iValue ) == tinyxml2::XML_SUCCESS )
							*(TINT*)rSetting.pValue = iValue;
						break;
					}
					case LevelSetting::T_FLOAT:
					{
						TFLOAT fValue;
						if ( pLevel->QueryFloatAttribute( szAttr, &fValue ) == tinyxml2::XML_SUCCESS )
							( (TFLOAT*)rSetting.pValue )[ iElem ] = fValue;
						break;
					}
				}
			}
		}

		break;
	}
}

void LevelSettings_SaveCurrentLevel( const TCHAR* a_szPath )
{
	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 )
		return;
	const TCHAR* szLevelName = CubemapAnchors_GetLevelName( iLevel );

	// Load the existing file first so other levels' settings survive the write
	tinyxml2::XMLDocument oDoc;
	oDoc.LoadFile( a_szPath ); // missing file just means a fresh document

	tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "levelSettings" );
	if ( !pRoot )
	{
		pRoot = oDoc.NewElement( "levelSettings" );
		oDoc.InsertFirstChild( pRoot );
	}

	for ( tinyxml2::XMLElement* pLevel = pRoot->FirstChildElement( "level" ); pLevel; )
	{
		tinyxml2::XMLElement* pNext  = pLevel->NextSiblingElement( "level" );
		const TCHAR*          szName = pLevel->Attribute( "name" );
		if ( szName && LevelNamesEqual( szName, szLevelName ) )
			pRoot->DeleteChild( pLevel );
		pLevel = pNext;
	}

	tinyxml2::XMLElement* pLevel = oDoc.NewElement( "level" );
	pLevel->SetAttribute( "name", szLevelName );

	for ( const LevelSetting& rSetting : s_aLevelSettings )
	{
		for ( TINT iElem = 0; iElem < rSetting.iCount; iElem++ )
		{
			TCHAR szAttr[ 96 ];
			LevelSettingAttrName( rSetting, iElem, szAttr, sizeof( szAttr ) );

			switch ( rSetting.eType )
			{
				case LevelSetting::T_BOOL:
					pLevel->SetAttribute( szAttr, *(TBOOL*)rSetting.pValue != TFALSE );
					break;
				case LevelSetting::T_INT:
					pLevel->SetAttribute( szAttr, *(TINT*)rSetting.pValue );
					break;
				case LevelSetting::T_FLOAT:
					pLevel->SetAttribute( szAttr, ( (TFLOAT*)rSetting.pValue )[ iElem ] );
					break;
			}
		}
	}

	pRoot->InsertEndChild( pLevel );
	oDoc.SaveFile( a_szPath );
}

static constexpr const TCHAR* LEVEL_SETTINGS_PATH = "Data\\LevelSettings.xml";

static constexpr TINT MAX_FX_SLOTS = 64; // > total scalar elements in s_aFXSettings

static TCHAR s_szCurrentFXName[ 64 ] = "";

const TCHAR* FXSettings_GetCurrentName()
{
	return s_szCurrentFXName;
}

// Bools/ints are carried as floats through the blend; writing rounds/thresholds them back
static TFLOAT FXSlotGet( const LevelSetting& a_rSetting, TINT a_iElem )
{
	switch ( a_rSetting.eType )
	{
		case LevelSetting::T_BOOL: return *(TBOOL*)a_rSetting.pValue ? 1.0f : 0.0f;
		case LevelSetting::T_INT: return ( TFLOAT ) * (TINT*)a_rSetting.pValue;
		default: return ( (TFLOAT*)a_rSetting.pValue )[ a_iElem ];
	}
}

static void FXSlotSet( const LevelSetting& a_rSetting, TINT a_iElem, TFLOAT a_fValue )
{
	switch ( a_rSetting.eType )
	{
		case LevelSetting::T_BOOL: *(TBOOL*)a_rSetting.pValue = a_fValue > 0.5f; break;
		case LevelSetting::T_INT: *(TINT*)a_rSetting.pValue = (TINT)( a_fValue + 0.5f ); break;
		default: ( (TFLOAT*)a_rSetting.pValue )[ a_iElem ] = a_fValue; break;
	}
}

// Captured once from the live globals' start-up values; FX and per-level blocks fall back to these
static constexpr TINT MAX_LEVEL_SLOTS = 256; // > total scalar elements in s_aLevelSettings

static TFLOAT s_aflFXDefault[ MAX_FX_SLOTS ]       = {};
static TFLOAT s_aflLevelDefault[ MAX_LEVEL_SLOTS ] = {};
static TBOOL  s_bDefaultsCaptured                  = TFALSE;

static void Settings_CaptureBuffer( const LevelSetting* a_pSettings, TINT a_nSettings, TFLOAT* a_pOut )
{
	TINT iSlot = 0;
	for ( TINT i = 0; i < a_nSettings; i++ )
		for ( TINT iElem = 0; iElem < a_pSettings[ i ].iCount; iElem++ )
			a_pOut[ iSlot++ ] = FXSlotGet( a_pSettings[ i ], iElem );
}

static void Settings_ApplyBuffer( const LevelSetting* a_pSettings, TINT a_nSettings, const TFLOAT* a_pIn )
{
	TINT iSlot = 0;
	for ( TINT i = 0; i < a_nSettings; i++ )
		for ( TINT iElem = 0; iElem < a_pSettings[ i ].iCount; iElem++ )
			FXSlotSet( a_pSettings[ i ], iElem, a_pIn[ iSlot++ ] );
}

static void Settings_EnsureDefaults()
{
	if ( s_bDefaultsCaptured )
		return;
	Settings_CaptureBuffer( s_aFXSettings, TARRAYSIZE( s_aFXSettings ), s_aflFXDefault );
	Settings_CaptureBuffer( s_aLevelSettings, TARRAYSIZE( s_aLevelSettings ), s_aflLevelDefault );
	s_bDefaultsCaptured = TTRUE;
}

static void LevelSettings_ResetToDefaults()
{
	Settings_EnsureDefaults();
	Settings_ApplyBuffer( s_aLevelSettings, TARRAYSIZE( s_aLevelSettings ), s_aflLevelDefault );
}

// Fills a_pOutSlots with the named FX's saved values (defaults for any it omits); returns
// TFALSE when the FX has no saved block at all
static TBOOL FXSettings_ReadBlock( const TCHAR* a_szFXName, TFLOAT* a_pOutSlots )
{
	Settings_EnsureDefaults();
	TINT iSlot = 0;
	for ( const LevelSetting& rSetting : s_aFXSettings )
		for ( TINT iElem = 0; iElem < rSetting.iCount; iElem++, iSlot++ )
			a_pOutSlots[ iSlot ] = s_aflFXDefault[ iSlot ];

	tinyxml2::XMLDocument oDoc;
	if ( oDoc.LoadFile( LEVEL_SETTINGS_PATH ) != tinyxml2::XML_SUCCESS )
		return TFALSE;

	const tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "levelSettings" );
	if ( !pRoot )
		return TFALSE;

	for ( const tinyxml2::XMLElement* pFX = pRoot->FirstChildElement( "fx" );
	      pFX;
	      pFX = pFX->NextSiblingElement( "fx" ) )
	{
		const TCHAR* szName = pFX->Attribute( "name" );
		if ( !szName || !LevelNamesEqual( szName, a_szFXName ) )
			continue;

		iSlot = 0;
		for ( const LevelSetting& rSetting : s_aFXSettings )
		{
			for ( TINT iElem = 0; iElem < rSetting.iCount; iElem++, iSlot++ )
			{
				TCHAR szAttr[ 96 ];
				LevelSettingAttrName( rSetting, iElem, szAttr, sizeof( szAttr ) );

				TFLOAT fValue;
				if ( pFX->QueryFloatAttribute( szAttr, &fValue ) == tinyxml2::XML_SUCCESS )
					a_pOutSlots[ iSlot ] = fValue;
				else
				{
					TBOOL bValue;
					if ( pFX->QueryBoolAttribute( szAttr, &bValue ) == tinyxml2::XML_SUCCESS )
						a_pOutSlots[ iSlot ] = bValue ? 1.0f : 0.0f;
				}
			}
		}
		return TTRUE;
	}

	return TFALSE;
}

// Preserves every other <fx> and <level> block in the file
void FXSettings_SaveCurrentFX()
{
	if ( s_szCurrentFXName[ 0 ] == '\0' )
		return;

	tinyxml2::XMLDocument oDoc;
	oDoc.LoadFile( LEVEL_SETTINGS_PATH ); // missing file just means a fresh document

	tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "levelSettings" );
	if ( !pRoot )
	{
		pRoot = oDoc.NewElement( "levelSettings" );
		oDoc.InsertFirstChild( pRoot );
	}

	for ( tinyxml2::XMLElement* pFX = pRoot->FirstChildElement( "fx" ); pFX; )
	{
		tinyxml2::XMLElement* pNext  = pFX->NextSiblingElement( "fx" );
		const TCHAR*          szName = pFX->Attribute( "name" );
		if ( szName && LevelNamesEqual( szName, s_szCurrentFXName ) )
			pRoot->DeleteChild( pFX );
		pFX = pNext;
	}

	tinyxml2::XMLElement* pFX = oDoc.NewElement( "fx" );
	pFX->SetAttribute( "name", s_szCurrentFXName );

	for ( const LevelSetting& rSetting : s_aFXSettings )
	{
		for ( TINT iElem = 0; iElem < rSetting.iCount; iElem++ )
		{
			TCHAR szAttr[ 96 ];
			LevelSettingAttrName( rSetting, iElem, szAttr, sizeof( szAttr ) );

			switch ( rSetting.eType )
			{
				case LevelSetting::T_BOOL:
					pFX->SetAttribute( szAttr, *(TBOOL*)rSetting.pValue != TFALSE );
					break;
				case LevelSetting::T_INT:
					pFX->SetAttribute( szAttr, *(TINT*)rSetting.pValue );
					break;
				case LevelSetting::T_FLOAT:
					pFX->SetAttribute( szAttr, ( (TFLOAT*)rSetting.pValue )[ iElem ] );
					break;
			}
		}
	}

	pRoot->InsertEndChild( pFX );
	oDoc.SaveFile( LEVEL_SETTINGS_PATH );

	// On-disk block changed; drop the cache so future blends re-read it
	FXSettings_InvalidateCache();
}

//-----------------------------------------------------------------------------
// FX settings blend
//
// AGameTimeFXManager (singleton @0x00783d08) drives the day/night skybox + lighting.
// Reverse-engineered layout used below (OnUpdate @0x00545630, SetNewGameTime @0x005458C0):
//   +0x34  bool  m_bUpdateTime      day-phase blend is running
//   +0x38  float m_flTime           time elapsed in the current phase
//   +0x40  float m_StartTime        start of the phase's tail crossfade window
//   +0x44  float m_InvDuration      1 / (endTime - startTime) for that window
//   +0x6C  AGameTimeFX m_oPhaseFrom  CURRENT phase preset (held for most of the phase)
//   +0x1E4 AGameTimeFX m_oPhaseTo    NEXT phase preset (crossfaded to over the tail window)
//   +0x7A8 bool  m_bOverlayActive   day/night "junk" overlay is the steady state
//   +0x7A9 bool  m_bOverlayBlending overlay is fading in/out
//   +0x7AC float m_OverlayTime      overlay blend timer
//   +0x7B0 float m_OverlayDuration  overlay blend duration
//   +0xAA4 AGameTimeFX m_oOverlayTo  overlay target preset
// Each AGameTimeFX slot stores its preset name as a char* at +0x114.
//
// m_oPhaseTo is the NEXT phase, not the current one -- reading it as "current" runs a phase
// ahead and skips the current phase's block on level load / time changes
//-----------------------------------------------------------------------------

static TINT FXSettings_SlotCount()
{
	TINT iCount = 0;
	for ( const LevelSetting& rSetting : s_aFXSettings )
		iCount += rSetting.iCount;
	return iCount;
}

static void FXSettings_SeedDefault( TFLOAT* a_pOut )
{
	Settings_EnsureDefaults();
	const TINT iCount = FXSettings_SlotCount();
	for ( TINT i = 0; i < iCount; i++ )
		a_pOut[ i ] = s_aflFXDefault[ i ];
}

// By-name cache of saved blocks so we don't re-read the XML every frame during a blend
struct FXBlockCacheEntry
{
	TCHAR  szName[ 64 ];
	TBOOL  bValid;
	TBOOL  bHasBlock;
	TFLOAT aflSlots[ MAX_FX_SLOTS ];
};
static FXBlockCacheEntry s_aFXBlockCache[ 8 ] = {};

static void FXSettings_InvalidateCache()
{
	for ( FXBlockCacheEntry& rEntry : s_aFXBlockCache )
		rEntry.bValid = TFALSE;
}

// Loads + caches on demand; returns whether a saved <fx> block actually exists
static TBOOL FXSettings_GetBlock( const TCHAR* a_szFXName, TFLOAT* a_pOutSlots )
{
	if ( !a_szFXName || a_szFXName[ 0 ] == '\0' )
	{
		FXSettings_SeedDefault( a_pOutSlots );
		return TFALSE;
	}

	FXBlockCacheEntry* pEntry = TNULL;
	for ( FXBlockCacheEntry& rEntry : s_aFXBlockCache )
	{
		if ( rEntry.bValid && LevelNamesEqual( rEntry.szName, a_szFXName ) )
		{
			pEntry = &rEntry;
			break;
		}
	}

	if ( !pEntry )
	{
		// Cache miss: load into the next slot (round-robin eviction)
		static TINT s_iNextSlot = 0;
		pEntry     = &s_aFXBlockCache[ s_iNextSlot ];
		s_iNextSlot = ( s_iNextSlot + 1 ) % TARRAYSIZE( s_aFXBlockCache );

		T2String8::CopySafe( pEntry->szName, a_szFXName, sizeof( pEntry->szName ) );
		pEntry->bHasBlock = FXSettings_ReadBlock( a_szFXName, pEntry->aflSlots );
		pEntry->bValid    = TTRUE;
	}

	const TINT iCount = FXSettings_SlotCount();
	for ( TINT i = 0; i < iCount; i++ )
		a_pOutSlots[ i ] = pEntry->aflSlots[ i ];
	return pEntry->bHasBlock;
}

static void FXSettings_ApplySlots( const TFLOAT* a_pSlots )
{
	TINT iSlot = 0;
	for ( const LevelSetting& rSetting : s_aFXSettings )
		for ( TINT iElem = 0; iElem < rSetting.iCount; iElem++, iSlot++ )
			FXSlotSet( rSetting, iElem, a_pSlots[ iSlot ] );
}

static TFLOAT FXClamp01( TFLOAT a_f )
{
	return TMath::Max( 0.0f, TMath::Min( a_f, 1.0f ) );
}

// Block currently written into the live globals; empty = leave them editable (only overwrite while blending)
static TCHAR s_szAppliedFXName[ 64 ] = "";

// Reapply: re-read the preset's block on the next update; params kept for the existing call site
void OnGameFXChanged( const TCHAR* /*a_szFXName*/, TBOOL /*a_bForce*/ )
{
	FXSettings_InvalidateCache();
	s_szAppliedFXName[ 0 ] = '\0';
}

// Rebuilds the live FX settings from the game's manager slots; call once per frame
void FXSettings_Update( TFLOAT /*a_fDeltaTime*/ )
{
	const TUINTPTR pManager = *TREINTERPRETCAST( TUINTPTR*, 0x00783d08 );
	if ( !pManager )
		return;

	// Forced FX: interiors, minigames and scripted sequences pin a preset via SetForcedFX
	// (@0x00545230), which sets m_bSkipUpdate (+0x4D4) to freeze the day/night blend and shows
	// m_oForcedFX (+0x4D8) instead. Hold on its block directly (no day/overlay blend applies)
	if ( *TREINTERPRETCAST( TUINT8*, pManager + 0x4D4 ) != 0 )
	{
		const TCHAR* szForced = GameFXNameAt( pManager + 0x4D8 );

		T2String8::Format( s_szFXSlotsDebug, sizeof( s_szFXSlotsDebug ), "forced:%s", szForced ? szForced : "?" );

		if ( szForced )
		{
			T2String8::CopySafe( s_szCurrentFXName, szForced, sizeof( s_szCurrentFXName ) );
			if ( !LevelNamesEqual( szForced, s_szAppliedFXName ) )
			{
				TFLOAT aflBlock[ MAX_FX_SLOTS ];
				FXSettings_GetBlock( szForced, aflBlock );
				FXSettings_ApplySlots( aflBlock );
				T2String8::CopySafe( s_szAppliedFXName, szForced, sizeof( s_szAppliedFXName ) );
			}
		}
		return;
	}

	const TBOOL bUpdateTime      = *TREINTERPRETCAST( TUINT8*, pManager + 0x34 ) != 0;
	const TBOOL bOverlayActive   = *TREINTERPRETCAST( TUINT8*, pManager + 0x7A8 ) != 0;
	const TBOOL bOverlayBlending = *TREINTERPRETCAST( TUINT8*, pManager + 0x7A9 ) != 0;

	const TCHAR* szCurrent = GameFXNameAt( pManager + 0x6C );
	const TCHAR* szNext    = GameFXNameAt( pManager + 0x1E4 );
	const TCHAR* szOverlay = GameFXNameAt( pManager + 0xAA4 );

	// fPhaseT: 0 while holding the current preset, ramps to 1 across the phase tail toward the next (1 between phases)
	TFLOAT fPhaseT = 1.0f;
	if ( bUpdateTime )
	{
		const TFLOAT fTime  = *TREINTERPRETCAST( TFLOAT*, pManager + 0x38 );
		const TFLOAT fStart = *TREINTERPRETCAST( TFLOAT*, pManager + 0x40 );
		const TFLOAT fInv   = *TREINTERPRETCAST( TFLOAT*, pManager + 0x44 );
		fPhaseT             = FXClamp01( ( fTime - fStart ) * fInv );
	}

	// Overlay fade progress (mirrors OnUpdate's LAB_005457a8: reversed while fading out)
	TFLOAT fOverlayT = 0.0f;
	if ( bOverlayBlending )
	{
		const TFLOAT fDuration = *TREINTERPRETCAST( TFLOAT*, pManager + 0x7B0 );
		fOverlayT              = fDuration > 0.0f ? *TREINTERPRETCAST( TFLOAT*, pManager + 0x7AC ) / fDuration : 1.0f;
		if ( !bOverlayActive )
			fOverlayT = 1.0f - fOverlayT;
		fOverlayT = FXClamp01( fOverlayT );
	}

	const TCHAR* szDayDominant = ( fPhaseT >= 0.5f && szNext ) ? szNext : szCurrent;
	const TBOOL  bOverlayDom    = bOverlayActive || ( bOverlayBlending && fOverlayT >= 0.5f );
	const TCHAR* szDominant     = ( bOverlayDom && szOverlay ) ? szOverlay : szDayDominant;
	if ( szDominant )
		T2String8::CopySafe( s_szCurrentFXName, szDominant, sizeof( s_szCurrentFXName ) );

	T2String8::Format(
	    s_szFXSlotsDebug, sizeof( s_szFXSlotsDebug ),
	    "cur:%s next:%s t=%.2f  overlay(%s):%s ot=%.2f",
	    szCurrent ? szCurrent : "?", szNext ? szNext : "?", fPhaseT,
	    bOverlayActive ? "on" : ( bOverlayBlending ? "blend" : "off" ),
	    szOverlay ? szOverlay : "?", fOverlayT );

	const TBOOL bDayCrossfade = bUpdateTime && fPhaseT > 0.0f && fPhaseT < 1.0f;
	const TBOOL bBlending     = bDayCrossfade || bOverlayBlending;

	if ( !bBlending )
	{
		// Steady state: apply the dominant block once, then leave values editable so "Save For This FX" captures tweaks
		if ( szDominant && !LevelNamesEqual( szDominant, s_szAppliedFXName ) )
		{
			TFLOAT aflBlock[ MAX_FX_SLOTS ];
			FXSettings_GetBlock( szDominant, aflBlock );
			FXSettings_ApplySlots( aflBlock );
			T2String8::CopySafe( s_szAppliedFXName, szDominant, sizeof( s_szAppliedFXName ) );
		}
		return;
	}

	// Blend in progress: rebuild the live values every frame
	const TINT iCount = FXSettings_SlotCount();

	TFLOAT aflCur[ MAX_FX_SLOTS ];
	TFLOAT aflNext[ MAX_FX_SLOTS ];
	FXSettings_GetBlock( szCurrent, aflCur );
	FXSettings_GetBlock( szNext, aflNext );

	TFLOAT aflFinal[ MAX_FX_SLOTS ];
	for ( TINT i = 0; i < iCount; i++ )
		aflFinal[ i ] = aflCur[ i ] + ( aflNext[ i ] - aflCur[ i ] ) * fPhaseT;

	if ( bOverlayBlending || bOverlayActive )
	{
		TFLOAT aflOvl[ MAX_FX_SLOTS ];
		FXSettings_GetBlock( szOverlay, aflOvl );
		const TFLOAT fMix = bOverlayBlending ? fOverlayT : 1.0f;
		for ( TINT i = 0; i < iCount; i++ )
			aflFinal[ i ] = aflFinal[ i ] + ( aflOvl[ i ] - aflFinal[ i ] ) * fMix;
	}

	FXSettings_ApplySlots( aflFinal );
	s_szAppliedFXName[ 0 ] = '\0'; // force the resting block to re-apply once the blend settles
}

static void DrawRenderSettingsTab()
{
	// Reapply re-reads this level's snapshot, discarding unsaved tweaks
	if ( ImGui::Button( "Save For This Level" ) )
		LevelSettings_SaveCurrentLevel( "Data\\LevelSettings.xml" );
	ImGui::SameLine();
	if ( ImGui::Button( "Reapply" ) )
		LevelSettings_ApplyForCurrentLevel( "Data\\LevelSettings.xml" );
	ImGui::SameLine();
	{
		const TINT iLevel = CubemapAnchors_GetCurrentLevel();
		ImGui::TextDisabled( "Level: %s", iLevel >= 0 ? CubemapAnchors_GetLevelName( iLevel ) : "(none)" );
	}

	// Each section gets its own PushID scope: several reuse widget labels ("Shadow Intensity",
	// "Wind Direction", ...) which would otherwise share an ImGui ID and cross-drive each other
	ImGui::Separator();
	ImGui::TextUnformatted( "CSM Debugging" );
	ImGui::PushID( "CSMDebug" );

	const TCHAR* apCascadeModes[] = {
		"Normal CSM",
		"Force cascade 0",
		"Force cascade 1",
		"Force cascade 2",
	};

	TINT iCascadeMode = remaster::g_iCSMDebugCascade + 1;
	if ( ImGui::Combo( "Cascade Mode", &iCascadeMode, apCascadeModes, TARRAYSIZE( apCascadeModes ) ) )
		remaster::g_iCSMDebugCascade = iCascadeMode - 1;

	if ( remaster::g_iCSMDebugCascade >= 0 )
	{
		ImGui::Checkbox( "Use Full Shadow Range", &remaster::g_bCSMDebugFullRange );
		if ( !remaster::g_bCSMDebugFullRange )
			ImGui::Checkbox( "Mask By Split", &remaster::g_bCSMDebugMaskBySplit );
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "Sun Direction" );
	ImGui::PushID( "Sun" );
	ImGui::Checkbox( "Override Sun Direction", &remaster::g_bOverrideSunDirection );
	if ( remaster::g_bOverrideSunDirection )
	{
		ImGui::DragFloat( "Azimuth", &remaster::g_flSunAzimuth, 0.5f, 0.0f, 0.0f, "%.2f deg" );
		ImGui::DragFloat( "Elevation", &remaster::g_flSunElevation, 0.5f, 0.0f, 0.0f, "%.2f deg" );
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "Shadow Settings" );
	ImGui::PushID( "Shadows" );
	ImGui::Checkbox( "Enable CSM", &remaster::g_bCSMEnabled );
	ImGui::Checkbox( "Delayed Cascade Update", &remaster::g_bCSMDelayedCascadeUpdate );
	if ( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Rebuild far cascades every 4th/8th frame instead of every frame.\nCheaper; far shadows update with slight latency." );
	ImGui::SliderFloat( "Shadow Intensity", &remaster::g_flShadowIntensity, 0.0f, 1.0f );
	ImGui::DragFloat( "Shadow Distance", &remaster::g_flShadowDistance, 1.0f, 10.0f, 500.0f, "%.0f m" );
	ImGui::SliderFloat( "Split Lambda", &remaster::g_flShadowSplitLambda, 0.0f, 1.0f, "%.2f" );
	ImGui::SliderFloat( "Cascade Blend", &remaster::g_flShadowCascadeBlend, 0.0f, 0.5f, "%.2f" );
	ImGui::DragFloat( "Min Slope Depth Bias", &remaster::g_flShadowMinSlopeScaledDepthBias, 0.01f, 0.0f, 5.0f, "%.2f" );
	ImGui::SliderFloat( "Receiver Plane Bias", &remaster::g_flShadowReceiverPlaneBias, 0.0f, 2.0f, "%.2f" );
	ImGui::SliderFloat( "Normal Offset", &remaster::g_flShadowNormalOffsetScale, 0.0f, 8.0f, "%.2f texels" );
	ImGui::SliderFloat( "Grazing Scale", &remaster::g_flShadowGrazingScale, 1.0f, 16.0f, "%.1fx" );

	ImGui::TextUnformatted( "Per-Cascade Settings" );

	auto CascadeRow = [ & ]( const TCHAR* a_szLabel, TFLOAT* a_pValues, TFLOAT a_fSpeed, TFLOAT a_fMin, TFLOAT a_fMax, const TCHAR* a_szFormat ) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted( a_szLabel );
		for ( TINT i = 0; i < remaster::CSM_CASCADE_COUNT; i++ )
		{
			ImGui::TableNextColumn();
			ImGui::PushID( a_szLabel );
			ImGui::PushID( i );
			ImGui::SetNextItemWidth( -FLT_MIN );
			ImGui::DragFloat( "##v", &a_pValues[ i ], a_fSpeed, a_fMin, a_fMax, a_szFormat );
			ImGui::PopID();
			ImGui::PopID();
		}
	};

	if ( ImGui::BeginTable( "CSM Cascades", remaster::CSM_CASCADE_COUNT + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame ) )
	{
		const TCHAR* apCascadeHeaders[] = { "Param", "Cascade 0", "Cascade 1", "Cascade 2" };
		for ( TINT i = 0; i < remaster::CSM_CASCADE_COUNT + 1; i++ )
			ImGui::TableSetupColumn( apCascadeHeaders[ i ] );
		ImGui::TableHeadersRow();

		CascadeRow( "Cascade Pad", remaster::g_aflShadowCascadePadding, 0.25f, 0.0f, 50.0f, "%.1f m" );
		CascadeRow( "Caster Pad", remaster::g_aflShadowCasterPadding, 1.0f, 0.0f, 300.0f, "%.0f m" );
		CascadeRow( "Slope Bias", remaster::g_aflShadowSlopeScaledDepthBias, 0.01f, 0.0f, 5.0f, "%.2f" );
		CascadeRow( "Receiver Bias", remaster::g_aflShadowReceiverBias, 0.00005f, 0.0f, 0.01f, "%.5f" );
		CascadeRow( "PCF Radius", remaster::g_aflShadowPCFRadius, 1.0f, 1.0f, 3.0f, "%.0f" );

		ImGui::EndTable();
	}

	ImGui::PopID();
	ImGui::TextUnformatted( "Cloud Shadows" );
	ImGui::PushID( "Clouds" );
	ImGui::Checkbox( "Enable Cloud Shadows", &remaster::g_bCloudShadowsEnabled );
	if ( remaster::g_bCloudShadowsEnabled )
	{
		if ( !remaster::g_bCSMEnabled )
			ImGui::TextDisabled( "(requires CSM enabled)" );
		ImGui::Checkbox( "Clouds in Volumetric Fog", &remaster::g_bCloudShadowsVolumetrics );
		ImGui::SliderFloat( "Cloud Strength", &remaster::g_flCloudShadowStrength, 0.0f, 1.0f, "%.2f" );
		ImGui::DragFloat( "Cloud Region Size", &remaster::g_flCloudShadowRegionSize, 5.0f, 50.0f, 2000.0f, "%.0f m" );
		ImGui::SliderFloat( "Cloud Feature Scale", &remaster::g_flCloudShadowFeatureScale, 0.001f, 0.05f, "%.4f" );
		ImGui::SliderFloat( "Cloud Coverage", &remaster::g_flCloudShadowCoverage, -0.5f, 1.0f, "%.2f" );
		ImGui::SliderFloat( "Cloud Density", &remaster::g_flCloudShadowDensity, 0.0f, 16.0f, "%.1f" );
		ImGui::SliderFloat( "Cloud Contrast", &remaster::g_flCloudShadowContrast, 0.1f, 4.0f, "%.2f" );
		ImGui::SliderFloat( "Cloud Speed", &remaster::g_flCloudShadowSpeed, 0.0f, 0.5f, "%.3f" );
		ImGui::DragFloat2( "Cloud Wind Dir", remaster::g_flCloudShadowWindDir, 0.01f, -1.0f, 1.0f, "%.2f" );
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "Screen-Space AO" );
	ImGui::PushID( "AO" );
	ImGui::Checkbox( "Enable AO", &remaster::g_bHBAOEnabled );
	if ( remaster::g_bHBAOEnabled )
	{
		const TCHAR* apAOAlgorithms[] = {
			"HBAO+",
			"XeGTAO",
		};

		ImGui::Combo( "AO Algorithm", &remaster::g_iAOAlgorithm, apAOAlgorithms, TARRAYSIZE( apAOAlgorithms ) );
		ImGui::Checkbox( "Debug AO", &remaster::g_bHBAODebug );
		ImGui::DragFloat( "AO Radius", &remaster::g_flHBAORadius, 0.05f, 0.1f, 384.0f, "%.2f" );
		ImGui::DragFloat( "AO Scene Scale", &remaster::g_flHBAOSceneScale, 0.05f, 0.01f, 100.0f, "%.2f" );
		ImGui::SliderFloat( "AO Intensity", &remaster::g_flHBAOIntensity, 0.0f, 4.0f, "%.2f" );
		ImGui::SliderFloat( "AO Power", &remaster::g_flHBAOPower, 0.5f, 4.0f, "%.2f" );
		ImGui::SliderFloat( "AO Blur Sharpness", &remaster::g_flHBAOBlurSharpness, 0.0f, 16.0f, "%.2f" );

		if ( remaster::g_iAOAlgorithm == 1 )
		{
			ImGui::SliderFloat( "XeGTAO Radius Multiplier", &remaster::g_flXeGTAORadiusMultiplier, 0.3f, 3.0f, "%.3f" );
			ImGui::SliderFloat( "XeGTAO Falloff Range", &remaster::g_flXeGTAOFalloffRange, 0.05f, 1.0f, "%.3f" );
			ImGui::SliderFloat( "XeGTAO Distribution", &remaster::g_flXeGTAOSampleDistributionPower, 1.0f, 3.0f, "%.2f" );
			ImGui::SliderFloat( "XeGTAO Thin Occluder", &remaster::g_flXeGTAOThinOccluderCompensation, 0.0f, 0.7f, "%.2f" );
		}
		else
		{
			ImGui::SliderFloat( "AO Bias", &remaster::g_flHBAOBias, 0.0f, 0.5f, "%.3f" );
		}
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "Screen-Space Reflections (experimental)" );
	ImGui::PushID( "SSR" );
	ImGui::Checkbox( "Enable SSR", &remaster::g_bSSREnabled );
	if ( remaster::g_bSSREnabled )
	{
		ImGui::Checkbox( "Debug SSR (show reflection)", &remaster::g_bSSRDebug );
		ImGui::Checkbox( "Debug SSR Normals (G-buffer)", &remaster::g_bSSRDebugNormals );
		ImGui::SliderFloat( "SSR Intensity", &remaster::g_flSSRIntensity, 0.0f, 2.0f, "%.2f" );
		ImGui::DragFloat( "SSR Max Distance", &remaster::g_flSSRMaxDistance, 0.5f, 1.0f, 200.0f, "%.1f" );
		ImGui::SliderInt( "SSR Max Steps", &remaster::g_iSSRMaxSteps, 8, 256 );
		ImGui::DragFloat( "SSR Pixel Stride", &remaster::g_flSSRStepSize, 0.05f, 0.25f, 8.0f, "%.2f" );
		ImGui::DragFloat( "SSR Thickness", &remaster::g_flSSRThickness, 0.01f, 0.02f, 5.0f, "%.3f" );
		ImGui::SliderFloat( "SSR Fresnel Power", &remaster::g_flSSRFresnelPower, 0.0f, 8.0f, "%.2f" );
		ImGui::SliderFloat( "SSR Edge Fade", &remaster::g_flSSREdgeFade, 0.5f, 8.0f, "%.2f" );
	}

	ImGui::PopID();
	// Shared by SSR (fallback) and the world/skin IBL, so its controls live outside the SSR toggle
	ImGui::Separator();
	ImGui::TextUnformatted( "Reflection Cube (IBL + SSR fallback)" );
	ImGui::PushID( "SkyCube" );
	ImGui::Checkbox( "Sky Cube Enabled", &remaster::g_bSkyCubeEnabled );
	if ( remaster::g_bSkyCubeEnabled )
	{
		ImGui::Checkbox( "Sky Cube Debug View", &remaster::g_bSkyCubeDebugView );
		ImGui::Checkbox( "Reflect Terrain", &remaster::g_bReflectTerrain );
		ImGui::SliderFloat( "Sky Cube Intensity", &remaster::g_flSkyCubeIntensity, 0.0f, 4.0f, "%.2f" );
		ImGui::Checkbox( "Env Specular (IBL)", &remaster::g_bEnvSpecular );
		ImGui::SliderFloat( "Cube Blend Time", &remaster::g_flSkyCubeBlendTime, 0.0f, 2.0f, "%.2f s" );
		ImGui::SliderFloat( "Cube Refresh Time", &remaster::g_flSkyCubeRefreshTime, 0.06f, 2.0f, "%.2f s" );
		ImGui::DragFloat( "Cube Parallax Horiz", &remaster::g_flSkyCubeParallaxHorizontal, 0.5f, 1.0f, 500.0f, "%.1f" );
		ImGui::DragFloat( "Cube Parallax Vert", &remaster::g_flSkyCubeParallaxVertical, 0.5f, 1.0f, 500.0f, "%.1f" );
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "Tangents" );
	ImGui::Checkbox( "Debug Tangents (world)", &remaster::g_bDebugTangents );

	ImGui::Separator();
	ImGui::TextUnformatted( "Wind (per-material; blue vertex color = strength)" );
	ImGui::PushID( "Wind" );
	ImGui::Checkbox( "Enable Wind (master)", &remaster::g_bWindEnabled );
	if ( remaster::g_bWindEnabled )
	{
		ImGui::SliderFloat( "Wind Strength", &remaster::g_flWindStrength, 0.0f, 2.0f, "%.2f" );
		ImGui::SliderFloat( "Wind Speed", &remaster::g_flWindSpeed, 0.0f, 8.0f, "%.2f" );
		ImGui::DragFloat2( "Wind Direction", remaster::g_flWindDir, 0.01f, -1.0f, 1.0f, "%.2f" );
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "Glow Bloom" );
	ImGui::PushID( "GlowBloom" );
	ImGui::Checkbox( "Enable Glow Bloom", &remaster::g_bGlowBloomEnabled );
	if ( remaster::g_bGlowBloomEnabled )
	{
		ImGui::SliderInt( "Glow Blur Levels", &remaster::g_iGlowBloomKawaseLevels, 1, KAWASE_MAX_LEVELS );
		ImGui::SliderFloat( "Glow Blur Offset", &remaster::g_flGlowBloomKawaseOffset, 0.1f, 12.0f );
		ImGui::SliderFloat( "Glow Intensity", &remaster::g_flGlowBloomIntensity, 0.0f, 10.0f );
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "HDR Bloom" );
	ImGui::PushID( "HDRBloom" );
	ImGui::Checkbox( "Enable HDR Bloom", &remaster::g_bHDRBloomEnabled );
	if ( remaster::g_bHDRBloomEnabled )
	{
		ImGui::SliderInt( "HDR Blur Levels", &remaster::g_iHDRBloomKawaseLevels, 1, KAWASE_MAX_LEVELS );
		ImGui::SliderFloat( "HDR Blur Offset", &remaster::g_flHDRBloomKawaseOffset, 0.1f, 12.0f );
		ImGui::SliderFloat( "HDR Threshold", &remaster::g_flHDRBloomThreshold, 0.0f, 4.0f );
		ImGui::SliderFloat( "HDR Intensity", &remaster::g_flHDRBloomIntensity, 0.0f, 4.0f );
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "Dynamic Lights" );
	ImGui::PushID( "DynLights" );
	ImGui::Checkbox( "Enable Dynamic Lights", &remaster::g_bDynamicLightEnabled );
	if ( remaster::g_bDynamicLightEnabled )
	{
		ImGui::SliderFloat( "Light Intensity", &remaster::g_flDynamicLightIntensity, 0.0f, 3.0f, "%.2f" );
		ImGui::SliderFloat( "Volumetric Intensity", &remaster::g_flDynamicLightVolumetricIntensity, 0.0f, 1.0f, "%.3f" );
		ImGui::SliderFloat( "Bump Scale", &remaster::g_flDynamicLightBumpScale, 0.0f, 10.0f, "%.2f" );
		ImGui::ColorEdit3( "Light Color", remaster::g_flDynamicLightColor );
		ImGui::Checkbox( "Enable Dynamic Shadows", &remaster::g_bDynamicLightShadowsEnabled );
		if ( remaster::g_bDynamicLightShadowsEnabled )
		{
			ImGui::DragFloat( "Shadow Distance", &remaster::g_flDynamicLightShadowDistance, 1.0f, 1.0f, 150.0f, "%.0f m" );
			ImGui::SliderFloat( "Shadow Intensity", &remaster::g_flDynamicLightShadowIntensity, 0.0f, 1.0f, "%.2f" );
			ImGui::DragFloat( "Shadow Bias", &remaster::g_flDynamicLightShadowBias, 0.0001f, 0.0f, 0.02f, "%.4f" );
		}
		ImGui::Checkbox( "Enable Flicker", &remaster::g_bDynamicLightFlickerEnabled );
		if ( remaster::g_bDynamicLightFlickerEnabled )
		{
			ImGui::SliderFloat( "Flicker Speed", &remaster::g_flDynamicLightFlickerSpeed, 0.5f, 30.0f, "%.1f" );
			ImGui::SliderFloat( "Flicker Strength", &remaster::g_flDynamicLightFlickerStrength, 0.0f, 1.0f, "%.2f" );
		}
	}

	ImGui::PopID();
}

static void DrawFXTab()
{
	ImGui::Text( "Current FX: %s", s_szCurrentFXName[ 0 ] ? s_szCurrentFXName : "(none yet)" );
	if ( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Reported by the game's AGameTimeFXManager whenever it switches\nskybox/lighting presets. Saved settings apply per FX name, blended\nover the time below." );

	if ( s_szCurrentFXName[ 0 ] == '\0' )
		ImGui::TextDisabled( "Waiting for the game to select an FX preset..." );

	if ( s_szFXSlotsDebug[ 0 ] )
		ImGui::TextDisabled( "slots: %s", s_szFXSlotsDebug );

	// Reapply re-reads this FX's saved block from disk, discarding unsaved tweaks
	if ( ImGui::Button( "Save For This FX" ) )
		FXSettings_SaveCurrentFX();
	ImGui::SameLine();
	if ( ImGui::Button( "Reapply" ) )
		OnGameFXChanged( s_szCurrentFXName, TTRUE );

	ImGui::Separator();
	ImGui::TextUnformatted( "Sun Shafts" );
	ImGui::PushID( "SunShafts" );
	ImGui::Checkbox( "Enable Sun Shafts", &remaster::g_bSunShaftsEnabled );
	if ( remaster::g_bSunShaftsEnabled )
	{
		ImGui::SliderFloat( "Intensity", &remaster::g_flSunShaftsAlpha, 0.0f, 0.5f, "%.4f" );
		ImGui::SliderFloat( "Rays Length", &remaster::g_flSunShaftsRaysLength, 0.0f, 1.0f );
		ImGui::ColorEdit3( "Tint", remaster::g_flSunShaftsTint );
		ImGui::SliderInt( "Blur Levels", &remaster::g_iSunShaftsKawaseLevels, 1, KAWASE_MAX_LEVELS );
		ImGui::SliderFloat( "Blur Offset", &remaster::g_flSunShaftsKawaseOffset, 0.1f, 4.0f );
	}

	ImGui::PopID();
	ImGui::Separator();
	ImGui::TextUnformatted( "Volumetric Fog" );
	ImGui::PushID( "VolFog" );
	ImGui::Checkbox( "Enable Volumetric Fog", &remaster::g_bVolumetricFogEnabled );
	if ( remaster::g_bVolumetricFogEnabled )
	{
		const TCHAR* apVolumetricFogModes[] = {
			"Additive Light",
			"Darken Covered Areas",
		};

		ImGui::Combo( "Fog Composite", &remaster::g_iVolumetricFogCompositeMode, apVolumetricFogModes, TARRAYSIZE( apVolumetricFogModes ) );
		ImGui::DragFloat( "Fog Density", &remaster::g_flVolumetricFogDensity, 0.001f, 0.0f, 1.0f, "%.4f" );
		ImGui::SliderFloat( "Asymmetry (g)", &remaster::g_flVolumetricFogG, -0.99f, 0.99f, "%.2f" );
		ImGui::DragFloat( "Max Distance", &remaster::g_flVolumetricFogMaxDist, 1.0f, 1.0f, 500.0f, "%.0f m" );
		if ( remaster::g_iVolumetricFogCompositeMode == 1 )
		{
			ImGui::SliderFloat( "Darkening", &remaster::g_flVolumetricFogIntensity, 0.0f, 1.0f, "%.2f" );
		}
		else
		{
			ImGui::SliderFloat( "Intensity", &remaster::g_flVolumetricFogIntensity, 0.0f, 10.0f, "%.2f" );
			ImGui::Checkbox( "Use Scene Fog Color", &remaster::g_bVolumetricFogUseSceneColor );
			if ( ImGui::IsItemHovered() )
				ImGui::SetTooltip( "Colour the volumetrics with the level's distance-fog colour;\nthe colour below then multiplies it as a tint." );
			// "###" keeps the widget ID stable while the visible label switches with the mode
			ImGui::ColorEdit3( remaster::g_bVolumetricFogUseSceneColor ? "Modulation (multiply)###VolFogColor" : "Fog Color###VolFogColor", remaster::g_flVolumetricFogColor );
		}

		ImGui::SeparatorText( "Height" );
		ImGui::DragFloat( "Fog Bottom", &remaster::g_flVolumetricFogHeight, 0.1f, -100.0f, 200.0f, "%.1f m" );
		ImGui::DragFloat( "Fog Top", &remaster::g_flVolumetricFogTopHeight, 0.1f, -100.0f, 200.0f, "%.1f m" );

		ImGui::SeparatorText( "Noise / Wind" );
		ImGui::SliderFloat( "Noise Strength", &remaster::g_flVolumetricFogNoiseStrength, 0.0f, 1.0f, "%.2f" );
		if ( remaster::g_flVolumetricFogNoiseStrength > 0.0f )
		{
			ImGui::DragFloat( "Noise Scale", &remaster::g_flVolumetricFogNoiseScale, 0.005f, 0.005f, 1.0f, "%.3f" );
			ImGui::DragFloat2( "Wind Direction", remaster::g_flVolumetricFogWindDir, 0.05f, -1.0f, 1.0f, "%.2f" );
			ImGui::SliderFloat( "Wind Speed", &remaster::g_flVolumetricFogWindSpeed, 0.0f, 5.0f, "%.2f" );
		}
	}
	ImGui::PopID();
}

// Projection comes from ARenderer's render context (already computed); view is ACamera's
// world transform inverted
static TBOOL BuildGizmoMatrices( TMatrix44& a_rView, TMatrix44& a_rProj )
{
	ARenderer* pRenderer = ARenderer::GetSingleton();
	if ( !pRenderer || !pRenderer->m_pViewport )
		return TFALSE;

	auto* pContext = TSTATICCAST( remaster::RenderContextD3D11, pRenderer->m_pViewport->GetRenderContext() );
	if ( !pContext )
		return TFALSE;

	a_rProj = pContext->GetProjectionMatrix();

	// ACamera::m_Matrix is the view-world transform; invert it to the world-to-view matrix ImGuizmo expects
	TMatrix44 matViewWorld = pContext->GetViewWorldMatrix();
	if ( ACameraManager* pCamMgr = ACameraManager::GetSingleton() )
	{
		if ( ACamera* pCamera = pCamMgr->GetCurrentCamera() )
			matViewWorld = pCamera->m_Matrix;
	}

	a_rView.InvertOrthogonal( matViewWorld );
	return TTRUE;
}

// Matches ImGuizmo's mapping (row-vector transform, Y flipped, full-display rect); returns
// false when the point is behind the camera so callers can drop the segment
static TBOOL WorldToScreen( const TMatrix44& a_rView, const TMatrix44& a_rProj, const TVector4& a_rWorld, ImVec2& a_rOut )
{
	TVector4 vView, vClip;
	TMatrix44::TransformVector( vView, a_rView, a_rWorld );
	TMatrix44::TransformVector( vClip, a_rProj, vView );

	if ( vClip.w <= 0.0001f )
		return TFALSE;

	const TFLOAT   fInv = 0.5f / vClip.w;
	const ImGuiIO& io   = ImGui::GetIO();

	a_rOut.x = ( vClip.x * fInv + 0.5f ) * io.DisplaySize.x;
	a_rOut.y = ( 1.0f - ( vClip.y * fInv + 0.5f ) ) * io.DisplaySize.y;
	return TTRUE;
}

// Wireframe sphere (three great circles) for a static light's influence radius; segments
// with an endpoint behind the camera are skipped rather than clipped
static void DrawWireSphere( const TMatrix44& a_rView, const TMatrix44& a_rProj, TFLOAT a_fCX, TFLOAT a_fCY, TFLOAT a_fCZ, TFLOAT a_fRadius, ImU32 a_uColor )
{
	static constexpr TINT SEGMENTS = 32;

	ImDrawList* pDrawList = ImGui::GetForegroundDrawList();

	for ( TINT iAxis = 0; iAxis < 3; iAxis++ )
	{
		ImVec2 vPrev;
		TBOOL  bPrevValid = TFALSE;

		for ( TINT i = 0; i <= SEGMENTS; i++ )
		{
			const TFLOAT fAngle = ( TFLOAT( i ) / SEGMENTS ) * 2.0f * TMath::PI;
			const TFLOAT fC     = TMath::Cos( fAngle ) * a_fRadius;
			const TFLOAT fS     = TMath::Sin( fAngle ) * a_fRadius;

			TVector4 vPoint;
			if ( iAxis == 0 ) vPoint = TVector4( a_fCX + fC, a_fCY + fS, a_fCZ, 1.0f );      // XY plane
			else if ( iAxis == 1 ) vPoint = TVector4( a_fCX, a_fCY + fC, a_fCZ + fS, 1.0f ); // YZ plane
			else vPoint = TVector4( a_fCX + fC, a_fCY, a_fCZ + fS, 1.0f );                   // XZ plane

			ImVec2      vScreen;
			const TBOOL bValid = WorldToScreen( a_rView, a_rProj, vPoint, vScreen );

			if ( bValid && bPrevValid )
				pDrawList->AddLine( vPrev, vScreen, a_uColor, 1.5f );

			vPrev      = vScreen;
			bPrevValid = bValid;
		}
	}
}

// Cubemap anchor parallax box; edges with an endpoint behind the camera are skipped rather than clipped
static void DrawWireBox( const TMatrix44& a_rView, const TMatrix44& a_rProj, const TVector4& a_rCenter, const TVector4& a_rHalf, ImU32 a_uColor )
{
	// 8 corners: bit 0=X, bit 1=Y, bit 2=Z sign
	TVector4 aCorners[ 8 ];
	for ( TINT i = 0; i < 8; i++ )
	{
		aCorners[ i ] = TVector4(
		    a_rCenter.x + ( ( i & 1 ) ? a_rHalf.x : -a_rHalf.x ),
		    a_rCenter.y + ( ( i & 2 ) ? a_rHalf.y : -a_rHalf.y ),
		    a_rCenter.z + ( ( i & 4 ) ? a_rHalf.z : -a_rHalf.z ),
		    1.0f
		);
	}

	static const TINT kEdges[ 12 ][ 2 ] = {
		{ 0, 1 },
		{ 2, 3 },
		{ 4, 5 },
		{ 6, 7 }, // X-aligned
		{ 0, 2 },
		{ 1, 3 },
		{ 4, 6 },
		{ 5, 7 }, // Y-aligned
		{ 0, 4 },
		{ 1, 5 },
		{ 2, 6 },
		{ 3, 7 }, // Z-aligned
	};

	ImDrawList* pDrawList = ImGui::GetForegroundDrawList();
	for ( TINT e = 0; e < 12; e++ )
	{
		ImVec2      vA, vB;
		const TBOOL bA = WorldToScreen( a_rView, a_rProj, aCorners[ kEdges[ e ][ 0 ] ], vA );
		const TBOOL bB = WorldToScreen( a_rView, a_rProj, aCorners[ kEdges[ e ][ 1 ] ], vB );
		if ( bA && bB )
			pDrawList->AddLine( vA, vB, a_uColor, 1.5f );
	}
}

// Selecting a light here clears the static/anchor selection so the gizmo only drives one at a time
static void DrawDynamicLightsTab()
{
	ImGui::Text( "Lights: %d / %d", s_iNumLights, MAX_EDITOR_LIGHTS );
	ImGui::SameLine();

	if ( s_iNumLights < MAX_EDITOR_LIGHTS )
	{
		if ( ImGui::Button( "Add Light" ) )
			AddLight();
	}
	else
	{
		ImGui::TextDisabled( "Add Light (max)" );
	}

	// Save preserves other levels' lights; Reload discards edits and re-reads the file
	ImGui::SameLine();
	if ( ImGui::Button( "Save" ) )
		DynamicLights_SaveCurrentLevel( "Data\\DynamicLights.xml" );
	ImGui::SameLine();
	if ( ImGui::Button( "Reload" ) )
		DynamicLights_LoadForCurrentLevel( "Data\\DynamicLights.xml", TFALSE );

	ImGui::Separator();

	TINT iToRemove = -1;

	for ( TINT i = 0; i < s_iNumLights; i++ )
	{
		EditorLight& light  = s_aLights[ i ];
		TBOOL        bDirty = TFALSE;

		ImGui::PushID( i );

		// Tint the header orange when selected, red when disabled
		if ( i == s_iSelectedLight )
			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.6f, 0.35f, 0.0f, 1.0f ) );
		else if ( !light.bEnabled )
			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.5f, 0.1f, 0.1f, 1.0f ) );

		const TBOOL bOpen = ImGui::CollapsingHeader( light.szName );

		if ( i == s_iSelectedLight || !light.bEnabled )
			ImGui::PopStyleColor();

		// Select this light for gizmo manipulation on header click
		if ( ImGui::IsItemClicked() )
		{
			s_iSelectedLight       = ( s_iSelectedLight == i ) ? -1 : i;
			s_iSelectedStaticLight = -1;
			s_iSelectedAnchor      = -1;
		}

		// Remove button on the same line as the header
		ImGui::SameLine( ImGui::GetWindowWidth() - 32.0f );
		if ( ImGui::SmallButton( "X" ) )
			iToRemove = i;

		if ( bOpen )
		{
			if ( ImGui::InputText( "Name##name", light.szName, sizeof( light.szName ) ) )
				bDirty = TTRUE;

			TBOOL bEnabled = (TBOOL)light.bEnabled;
			if ( ImGui::Checkbox( "Enabled", &bEnabled ) )
			{
				light.bEnabled = (TBOOL)bEnabled;
				bDirty         = TTRUE;
			}

			TBOOL bNight = (TBOOL)light.bNightOnly;
			if ( ImGui::Checkbox( "Night Only", &bNight ) )
			{
				light.bNightOnly = (TBOOL)bNight;
				bDirty           = TTRUE;
			}

			ImGui::Separator();
			ImGui::TextUnformatted( "Transform" );

			if ( ImGui::DragFloat3( "Position", light.vPosition, 0.1f ) )
				bDirty = TTRUE;

			if ( ImGui::DragFloat( "Azimuth", &light.flAzimuth, 1.0f, -360.0f, 360.0f, "%.1f deg" ) )
				bDirty = TTRUE;

			if ( ImGui::DragFloat( "Elevation", &light.flElevation, 1.0f, -90.0f, 90.0f, "%.1f deg" ) )
				bDirty = TTRUE;

			// Snap position + direction from the current camera transform
			ACamera* pCamera = ACameraManager::GetSingleton() ? ACameraManager::GetSingleton()->GetCurrentCamera() : TNULL;

			if ( pCamera )
			{
				if ( ImGui::Button( "Place at Camera" ) )
				{
					const TVector4& vPos = pCamera->m_Matrix.GetTranslation();
					const TVector4& vFwd = pCamera->m_Matrix.AsBasisVector4( BASISVECTOR_FORWARD );

					light.vPosition[ 0 ] = vPos.x;
					light.vPosition[ 1 ] = vPos.y;
					light.vPosition[ 2 ] = vPos.z;

					const TFLOAT fFwdY = TMath::Max( -1.0f, TMath::Min( 1.0f, vFwd.y ) );
					light.flElevation  = TMath::ASin( fFwdY ) * ( 180.0f / TMath::PI );
					light.flAzimuth    = TMath::ATan2( vFwd.x, vFwd.z ) * ( 180.0f / TMath::PI );

					bDirty = TTRUE;
				}
			}

			ImGui::Separator();
			ImGui::TextUnformatted( "Projection" );

			if ( ImGui::SliderFloat( "FOV", &light.flFOV, 5.0f, 170.0f, "%.1f deg" ) )
				bDirty = TTRUE;

			if ( ImGui::DragFloat( "Range", &light.flRange, 0.5f, 1.0f, 500.0f, "%.1f m" ) )
				bDirty = TTRUE;

			ImGui::Separator();
			ImGui::TextUnformatted( "Appearance" );

			if ( ImGui::ColorEdit3( "Color", light.settings.flColor ) )
				bDirty = TTRUE;

			if ( ImGui::SliderFloat( "Surface Intensity", &light.settings.flSurfaceIntensity, 0.0f, 10.0f, "%.3f" ) )
				bDirty = TTRUE;

			if ( ImGui::SliderFloat( "Volumetric Intensity", &light.settings.flVolumetricIntensity, 0.0f, 3.0f, "%.3f" ) )
				bDirty = TTRUE;

			if ( ImGui::SliderFloat( "Bump Scale", &light.settings.flBumpScale, 0.0f, 10.0f, "%.2f" ) )
				bDirty = TTRUE;

			ImGui::Separator();
			ImGui::TextUnformatted( "Flicker" );

			TBOOL bFlicker = (TBOOL)light.settings.bFlickerEnabled;
			if ( ImGui::Checkbox( "Flicker Enabled", &bFlicker ) )
			{
				light.settings.bFlickerEnabled = (TBOOL)bFlicker;
				bDirty                         = TTRUE;
			}

			if ( light.settings.bFlickerEnabled )
			{
				if ( ImGui::SliderFloat( "Flicker Speed", &light.settings.flFlickerSpeed, 0.5f, 30.0f, "%.1f" ) )
					bDirty = TTRUE;

				if ( ImGui::SliderFloat( "Flicker Strength", &light.settings.flFlickerStrength, 0.0f, 1.0f, "%.3f" ) )
					bDirty = TTRUE;
			}

			ImGui::Separator();
			ImGui::TextUnformatted( "Shadow" );

			if ( ImGui::SliderFloat( "Shadow Intensity", &light.settings.flShadowIntensity, 0.0f, 1.0f, "%.3f" ) )
				bDirty = TTRUE;

			if ( ImGui::DragFloat( "Shadow Bias", &light.settings.flShadowBias, 0.00005f, 0.0f, 0.01f, "%.5f" ) )
				bDirty = TTRUE;
		}

		if ( bDirty )
			ApplyLightToGlowObject( light );

		ImGui::PopID();
	}

	if ( iToRemove >= 0 )
	{
		if ( s_iSelectedLight == iToRemove )
			s_iSelectedLight = -1;
		else if ( s_iSelectedLight > iToRemove )
			s_iSelectedLight--;

		RemoveLight( iToRemove );
	}
}

// Static point lights live on the LightManager (loaded from level data, never move), edited in place
static void DrawStaticLightsTab()
{
	if ( !g_pLightManager )
	{
		ImGui::TextDisabled( "Light manager not ready." );
		return;
	}

	const TINT iCount = g_pLightManager->GetStaticPointLightCount();

	ImGui::Text( "Lights: %d / %d", iCount, MAX_STATIC_POINT_LIGHTS );
	ImGui::SameLine();

	if ( iCount < MAX_STATIC_POINT_LIGHTS )
	{
		if ( ImGui::Button( "Add Light" ) )
		{
			StaticPointLight light = {};
			light.vPosition        = TVector4( 0.0f, 0.0f, 0.0f, 10.0f ); // w = radius
			light.vColor           = TVector4( 1.0f, 1.0f, 1.0f, 1.0f );  // w = intensity
			light.uiFlags          = STATIC_LIGHT_ENABLED;
			light.iLightMag        = STATIC_LIGHT_NO_MAG; // always on until bound to a building-light group

			if ( ACameraManager::GetSingleton() )
			{
				if ( ACamera* pCamera = ACameraManager::GetSingleton()->GetCurrentCamera() )
				{
					const TVector4& vPos = pCamera->m_Matrix.GetTranslation();
					light.vPosition.x    = vPos.x;
					light.vPosition.y    = vPos.y;
					light.vPosition.z    = vPos.z;
				}
			}

			const TINT iNew = g_pLightManager->AddStaticPointLight( light );
			if ( iNew >= 0 )
			{
				s_iSelectedStaticLight = iNew;
				s_iSelectedLight       = -1;
				s_iSelectedAnchor      = -1;
			}
		}
	}
	else
	{
		ImGui::TextDisabled( "Add Light (max)" );
	}

	// Save preserves other levels' lights; Reload discards edits and re-reads the file
	ImGui::SameLine();
	if ( ImGui::Button( "Save" ) )
		StaticLights_SaveCurrentLevel( "Data\\StaticLights.xml" );
	ImGui::SameLine();
	if ( ImGui::Button( "Reload" ) )
	{
		StaticLights_LoadForCurrentLevel( "Data\\StaticLights.xml" );
		s_iSelectedStaticLight = -1;
	}

	ImGui::Separator();

	TINT iToRemove = -1;

	for ( TINT i = 0; i < iCount; i++ )
	{
		StaticPointLight& light    = g_pLightManager->GetStaticPointLight( i );
		TBOOL             bEnabled = ( light.uiFlags & STATIC_LIGHT_ENABLED ) != 0;

		ImGui::PushID( i );

		// Tint the header orange when selected, red when disabled
		if ( i == s_iSelectedStaticLight )
			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.6f, 0.35f, 0.0f, 1.0f ) );
		else if ( !bEnabled )
			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.5f, 0.1f, 0.1f, 1.0f ) );

		TCHAR szName[ 32 ];
		_snprintf_s( szName, sizeof( szName ), _TRUNCATE, "Static Light %d", i + 1 );
		const TBOOL bOpen = ImGui::CollapsingHeader( szName );

		if ( i == s_iSelectedStaticLight || !bEnabled )
			ImGui::PopStyleColor();

		// Select this light for gizmo manipulation on header click
		if ( ImGui::IsItemClicked() )
		{
			s_iSelectedStaticLight = ( s_iSelectedStaticLight == i ) ? -1 : i;
			s_iSelectedLight       = -1;
			s_iSelectedAnchor      = -1;
		}

		// Remove button on the same line as the header
		ImGui::SameLine( ImGui::GetWindowWidth() - 32.0f );
		if ( ImGui::SmallButton( "X" ) )
			iToRemove = i;

		if ( bOpen )
		{
			if ( ImGui::Checkbox( "Enabled", &bEnabled ) )
			{
				if ( bEnabled ) light.uiFlags |= STATIC_LIGHT_ENABLED;
				else light.uiFlags &= ~STATIC_LIGHT_ENABLED;
			}

			// When set, this light only lights while its group's window lights do (game ramps
			// ATerrainInterface::m_afLightMags on a schedule), superseding the Night Only gate below
			static const TCHAR* s_apszLightMagNames[ STATIC_LIGHT_MAG_COUNT ] = {
				"Farmhouse", "Neighbor", "Lodge", "Beady",
				"Barn", "Barn 2", "Cone Inner", "Cone Inner 2",
				"Cone Outer", "Cone Outer 2", "Nightlight", "Nightlight 2"
			};
			const TBOOL       bBound      = light.iLightMag >= 0 && light.iLightMag < STATIC_LIGHT_MAG_COUNT;
			const TCHAR*      szMagPreview = bBound ? s_apszLightMagNames[ light.iLightMag ] : "Always On";
			if ( ImGui::BeginCombo( "Light Group", szMagPreview ) )
			{
				if ( ImGui::Selectable( "Always On", !bBound ) )
					light.iLightMag = STATIC_LIGHT_NO_MAG;
				for ( TINT m = 0; m < STATIC_LIGHT_MAG_COUNT; m++ )
				{
					if ( ImGui::Selectable( s_apszLightMagNames[ m ], light.iLightMag == m ) )
						light.iLightMag = TINT8( m );
				}
				ImGui::EndCombo();
			}

			// Night Only only matters for unbound lights; a group binding already carries a schedule
			ImGui::BeginDisabled( bBound );
			TBOOL bNightOnly = ( light.uiFlags & STATIC_LIGHT_NIGHT_ONLY ) != 0;
			if ( ImGui::Checkbox( "Night Only", &bNightOnly ) )
			{
				if ( bNightOnly ) light.uiFlags |= STATIC_LIGHT_NIGHT_ONLY;
				else light.uiFlags &= ~STATIC_LIGHT_NIGHT_ONLY;
			}
			ImGui::EndDisabled();

			ImGui::Separator();
			ImGui::TextUnformatted( "Transform" );

			ImGui::DragFloat3( "Position", &light.vPosition.x, 0.1f );
			ImGui::DragFloat( "Radius", &light.vPosition.w, 0.1f, 0.1f, 500.0f, "%.1f m" );

			if ( ACameraManager::GetSingleton() )
			{
				if ( ACamera* pCamera = ACameraManager::GetSingleton()->GetCurrentCamera() )
				{
					if ( ImGui::Button( "Place at Camera" ) )
					{
						const TVector4& vPos = pCamera->m_Matrix.GetTranslation();
						light.vPosition.x    = vPos.x;
						light.vPosition.y    = vPos.y;
						light.vPosition.z    = vPos.z;
					}
				}
			}

			ImGui::Separator();
			ImGui::TextUnformatted( "Appearance" );

			ImGui::ColorEdit3( "Color", &light.vColor.x );
			ImGui::SliderFloat( "Intensity", &light.vColor.w, 0.0f, 10.0f, "%.3f" );
		}

		ImGui::PopID();
	}

	if ( iToRemove >= 0 )
	{
		if ( s_iSelectedStaticLight == iToRemove )
			s_iSelectedStaticLight = -1;
		else if ( s_iSelectedStaticLight > iToRemove )
			s_iSelectedStaticLight--;

		g_pLightManager->RemoveStaticPointLight( iToRemove );
	}
}

// Each anchor is a world point the cubemap is captured at, with a parallax box; the nearest
// active anchor wins at runtime (persisted to Data\CubemapAnchors.xml)
static void DrawCubemapTab()
{
	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 )
	{
		ImGui::TextDisabled( "No level loaded." );
		return;
	}

	ImGui::Text( "Level: %s", CubemapAnchors_GetLevelName( iLevel ) );

	const TINT iCount = CubemapAnchors_GetCount();
	ImGui::Text( "Anchors: %d / %d", iCount, MAX_ANCHORS_PER_LEVEL );
	ImGui::SameLine();

	if ( iCount < MAX_ANCHORS_PER_LEVEL )
	{
		if ( ImGui::Button( "Add Anchor" ) )
		{
			CubemapAnchor anchor = {};
			anchor.vPosition     = TVector4( 0.0f, 0.0f, 0.0f, 50.0f ); // w = influence radius (trigger region)
			anchor.vHalfExtents  = TVector4( 40.0f, 20.0f, 40.0f, 0.0f );

			if ( ACameraManager::GetSingleton() )
			{
				if ( ACamera* pCamera = ACameraManager::GetSingleton()->GetCurrentCamera() )
				{
					const TVector4& vPos = pCamera->m_Matrix.GetTranslation();
					anchor.vPosition.x   = vPos.x;
					anchor.vPosition.y   = vPos.y;
					anchor.vPosition.z   = vPos.z;
				}
			}

			const TINT iNew = CubemapAnchors_Add( anchor );
			if ( iNew >= 0 )
			{
				s_iSelectedAnchor      = iNew;
				s_iSelectedLight       = -1;
				s_iSelectedStaticLight = -1;
			}
		}
	}
	else
	{
		ImGui::TextDisabled( "Add Anchor (max)" );
	}

	ImGui::SameLine();
	if ( ImGui::Button( "Save" ) )
		CubemapAnchors_Save( "Data\\CubemapAnchors.xml" );

	ImGui::Separator();

	TINT iToRemove = -1;

	for ( TINT i = 0; i < iCount; i++ )
	{
		CubemapAnchor& anchor = CubemapAnchors_Get( i );

		ImGui::PushID( i );

		if ( i == s_iSelectedAnchor )
			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.6f, 0.35f, 0.0f, 1.0f ) );

		TCHAR szName[ 32 ];
		_snprintf_s( szName, sizeof( szName ), _TRUNCATE, "Anchor %d", i + 1 );
		const TBOOL bOpen = ImGui::CollapsingHeader( szName );

		if ( i == s_iSelectedAnchor )
			ImGui::PopStyleColor();

		if ( ImGui::IsItemClicked() )
		{
			s_iSelectedAnchor      = ( s_iSelectedAnchor == i ) ? -1 : i;
			s_iSelectedLight       = -1;
			s_iSelectedStaticLight = -1;
		}

		ImGui::SameLine( ImGui::GetWindowWidth() - 32.0f );
		if ( ImGui::SmallButton( "X" ) )
			iToRemove = i;

		if ( bOpen )
		{
			ImGui::TextUnformatted( "Transform" );
			ImGui::DragFloat3( "Position", &anchor.vPosition.x, 0.1f );
			// Camera must be within this sphere for the anchor to activate; outside every sphere it falls back to a camera probe
			ImGui::DragFloat( "Influence Radius", &anchor.vPosition.w, 0.5f, 0.5f, 1000.0f, "%.1f m" );

			if ( ACameraManager::GetSingleton() )
			{
				if ( ACamera* pCamera = ACameraManager::GetSingleton()->GetCurrentCamera() )
				{
					if ( ImGui::Button( "Place at Camera" ) )
					{
						const TVector4& vPos = pCamera->m_Matrix.GetTranslation();
						anchor.vPosition.x   = vPos.x;
						anchor.vPosition.y   = vPos.y;
						anchor.vPosition.z   = vPos.z;
					}
				}
			}

			ImGui::Separator();
			ImGui::TextUnformatted( "Parallax Box (half-extents)" );
			ImGui::DragFloat( "Half X", &anchor.vHalfExtents.x, 0.5f, 0.5f, 500.0f, "%.1f m" );
			ImGui::DragFloat( "Half Y", &anchor.vHalfExtents.y, 0.5f, 0.5f, 500.0f, "%.1f m" );
			ImGui::DragFloat( "Half Z", &anchor.vHalfExtents.z, 0.5f, 0.5f, 500.0f, "%.1f m" );
		}

		ImGui::PopID();
	}

	if ( iToRemove >= 0 )
	{
		if ( s_iSelectedAnchor == iToRemove )
			s_iSelectedAnchor = -1;
		else if ( s_iSelectedAnchor > iToRemove )
			s_iSelectedAnchor--;

		CubemapAnchors_Remove( iToRemove );
	}
}

void Render()
{
	if ( !g_bEnabled )
		return;

	// -----------------------------------------------------------------------
	// ImGuizmo frame setup -- must happen before any Manipulate() call
	// -----------------------------------------------------------------------
	ImGuizmo::BeginFrame();
	ImGuizmo::SetOrthographic( false );

	const ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect( 0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y );

	ImGui::SetNextWindowSize( ImVec2( 390.0f, 540.0f ), ImGuiCond_FirstUseEver );
	ImGui::Begin( "Level Settings" );

	// Gizmo operation selector (shared by the light tabs)
	ImGui::TextUnformatted( "Gizmo:" );
	ImGui::SameLine();
	if ( ImGui::RadioButton( "Translate", s_eGizmoOp == ImGuizmo::TRANSLATE ) ) s_eGizmoOp = ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	if ( ImGui::RadioButton( "Rotate", s_eGizmoOp == ImGuizmo::ROTATE ) ) s_eGizmoOp = ImGuizmo::ROTATE;

	// The visualisations and gizmos below only draw for the open tab, so they don't clutter other tabs
	enum ActiveTab
	{
		TAB_NONE,
		TAB_DYNAMIC,
		TAB_STATIC,
		TAB_CUBEMAP,
		TAB_SETTINGS
	};
	ActiveTab eActiveTab = TAB_NONE;

	ImGui::Separator();
	if ( ImGui::BeginTabBar( "LightTabs" ) )
	{
		if ( ImGui::BeginTabItem( "Dynamic" ) )
		{
			eActiveTab = TAB_DYNAMIC;
			DrawDynamicLightsTab();
			ImGui::EndTabItem();
		}

		if ( ImGui::BeginTabItem( "Static" ) )
		{
			eActiveTab = TAB_STATIC;
			DrawStaticLightsTab();
			ImGui::EndTabItem();
		}

		if ( ImGui::BeginTabItem( "Cubemap" ) )
		{
			eActiveTab = TAB_CUBEMAP;
			DrawCubemapTab();
			ImGui::EndTabItem();
		}

		if ( ImGui::BeginTabItem( "FX" ) )
		{
			DrawFXTab();
			ImGui::EndTabItem();
		}

		if ( ImGui::BeginTabItem( "Settings" ) )
		{
			eActiveTab = TAB_SETTINGS;
			DrawRenderSettingsTab();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();

	// -----------------------------------------------------------------------
	// Static light radius spheres -- Static tab only (lights have no in-world shading yet)
	// -----------------------------------------------------------------------
	if ( eActiveTab == TAB_STATIC && g_pLightManager && g_pLightManager->GetStaticPointLightCount() > 0 )
	{
		TMatrix44 oView, oProj;
		if ( BuildGizmoMatrices( oView, oProj ) )
		{
			for ( TINT i = 0; i < g_pLightManager->GetStaticPointLightCount(); i++ )
			{
				const StaticPointLight& light    = g_pLightManager->GetStaticPointLight( i );
				const TBOOL             bEnabled = ( light.uiFlags & STATIC_LIGHT_ENABLED ) != 0;

				const ImU32 uColor = ( i == s_iSelectedStaticLight ) ? IM_COL32( 255, 150, 0, 255 ) : bEnabled ? IM_COL32( 255, 225, 120, 200 ) :
				                                                                                                 IM_COL32( 150, 150, 150, 110 );

				DrawWireSphere( oView, oProj, light.vPosition.x, light.vPosition.y, light.vPosition.z, light.vPosition.w, uColor );
			}
		}
	}

	// -----------------------------------------------------------------------
	// ImGuizmo -- manipulate the selected light in world space (Dynamic tab only)
	// -----------------------------------------------------------------------
	if ( eActiveTab == TAB_DYNAMIC && s_iSelectedLight >= 0 && s_iSelectedLight < s_iNumLights )
	{
		TMatrix44 oView, oProj;
		if ( BuildGizmoMatrices( oView, oProj ) )
		{
			EditorLight& light = s_aLights[ s_iSelectedLight ];

			TMatrix44 oModel = BuildLightTransform( light.vPosition, light.flAzimuth, light.flElevation );

			const TFLOAT* pView  = reinterpret_cast<const TFLOAT*>( &oView );
			const TFLOAT* pProj  = reinterpret_cast<const TFLOAT*>( &oProj );
			TFLOAT*       pModel = reinterpret_cast<TFLOAT*>( &oModel );

			// Draw the gizmo into the foreground layer so it's always visible
			ImGuizmo::SetDrawlist( ImGui::GetForegroundDrawList() );

			if ( ImGuizmo::Manipulate( pView, pProj, s_eGizmoOp, ImGuizmo::WORLD, pModel ) )
			{
				light.vPosition[ 0 ] = oModel.m_f41;
				light.vPosition[ 1 ] = oModel.m_f42;
				light.vPosition[ 2 ] = oModel.m_f43;

				if ( s_eGizmoOp == ImGuizmo::ROTATE )
				{
					// Re-derive azimuth / elevation from the (possibly rotated) forward vector
					const TVector4& vFwd  = oModel.AsBasisVector4( BASISVECTOR_FORWARD );
					const TFLOAT    fFwdY = TMath::Max( -1.0f, TMath::Min( 1.0f, vFwd.y ) );
					light.flElevation     = TMath::ASin( fFwdY ) * ( 180.0f / TMath::PI );
					light.flAzimuth       = TMath::ATan2( vFwd.x, vFwd.z ) * ( 180.0f / TMath::PI );
				}

				ApplyLightToGlowObject( light );
			}
		}
	}

	// -----------------------------------------------------------------------
	// ImGuizmo -- translate the selected static light in world space (Static tab only)
	// -----------------------------------------------------------------------
	if ( eActiveTab == TAB_STATIC && g_pLightManager && s_iSelectedStaticLight >= 0 && s_iSelectedStaticLight < g_pLightManager->GetStaticPointLightCount() )
	{
		TMatrix44 oView, oProj;
		if ( BuildGizmoMatrices( oView, oProj ) )
		{
			StaticPointLight& light = g_pLightManager->GetStaticPointLight( s_iSelectedStaticLight );

			// Point lights have no orientation, so only the translation matters
			TMatrix44 oModel;
			oModel.Identity();
			oModel.m_f41 = light.vPosition.x;
			oModel.m_f42 = light.vPosition.y;
			oModel.m_f43 = light.vPosition.z;

			const TFLOAT* pView  = reinterpret_cast<const TFLOAT*>( &oView );
			const TFLOAT* pProj  = reinterpret_cast<const TFLOAT*>( &oProj );
			TFLOAT*       pModel = reinterpret_cast<TFLOAT*>( &oModel );

			ImGuizmo::SetDrawlist( ImGui::GetForegroundDrawList() );

			if ( ImGuizmo::Manipulate( pView, pProj, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, pModel ) )
			{
				light.vPosition.x = oModel.m_f41;
				light.vPosition.y = oModel.m_f42;
				light.vPosition.z = oModel.m_f43;
			}
		}
	}

	// -----------------------------------------------------------------------
	// Cubemap anchor box -- Cubemap tab only, selected anchor only (avoid probe clutter)
	// -----------------------------------------------------------------------
	if ( eActiveTab == TAB_CUBEMAP && s_iSelectedAnchor >= 0 && s_iSelectedAnchor < CubemapAnchors_GetCount() )
	{
		TMatrix44 oView, oProj;
		if ( BuildGizmoMatrices( oView, oProj ) )
		{
			const CubemapAnchor& anchor = CubemapAnchors_Get( s_iSelectedAnchor );
			// Blue box = parallax proxy; green sphere = influence/trigger region (radius = w)
			DrawWireBox( oView, oProj, anchor.vPosition, anchor.vHalfExtents, IM_COL32( 0, 200, 255, 255 ) );
			DrawWireSphere( oView, oProj, anchor.vPosition.x, anchor.vPosition.y, anchor.vPosition.z, anchor.vPosition.w, IM_COL32( 80, 255, 140, 220 ) );
		}
	}

	// -----------------------------------------------------------------------
	// ImGuizmo -- translate the selected cubemap anchor in world space (Cubemap tab only)
	// -----------------------------------------------------------------------
	if ( eActiveTab == TAB_CUBEMAP && s_iSelectedAnchor >= 0 && s_iSelectedAnchor < CubemapAnchors_GetCount() )
	{
		TMatrix44 oView, oProj;
		if ( BuildGizmoMatrices( oView, oProj ) )
		{
			CubemapAnchor& anchor = CubemapAnchors_Get( s_iSelectedAnchor );

			TMatrix44 oModel;
			oModel.Identity();
			oModel.m_f41 = anchor.vPosition.x;
			oModel.m_f42 = anchor.vPosition.y;
			oModel.m_f43 = anchor.vPosition.z;

			const TFLOAT* pView  = reinterpret_cast<const TFLOAT*>( &oView );
			const TFLOAT* pProj  = reinterpret_cast<const TFLOAT*>( &oProj );
			TFLOAT*       pModel = reinterpret_cast<TFLOAT*>( &oModel );

			ImGuizmo::SetDrawlist( ImGui::GetForegroundDrawList() );

			if ( ImGuizmo::Manipulate( pView, pProj, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, pModel ) )
			{
				anchor.vPosition.x = oModel.m_f41;
				anchor.vPosition.y = oModel.m_f42;
				anchor.vPosition.z = oModel.m_f43;
			}
		}
	}
}

} // namespace editor
