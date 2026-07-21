#include "pch.h"
#include "GameSettings.h"
#include "RenderDX11.h"
#include "RenderParams.h"
#include "LightManager.h"
#include "CSM/CSMManager.h"

#include <ToshiTools/tinyxml2.h>

#include <BYardSDK/AGUI2.h>

#include <Toshi/T2String8.h>

#include <imgui.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

using namespace remaster;

static GraphicsSettings s_oDisplay;
static TBOOL            s_bHasDisplay = TFALSE;

static TBOOL s_bUserCSMEnabled                 = TTRUE;
static TBOOL s_bUserCloudShadowsEnabled        = TTRUE;
static TBOOL s_bUserAOEnabled                  = TTRUE;
static TBOOL s_bUserSSREnabled                 = TTRUE;
static TBOOL s_bUserHDRBloomEnabled            = TTRUE;
static TBOOL s_bUserSunShaftsEnabled           = TTRUE;
static TBOOL s_bUserVolumetricFogEnabled       = TTRUE;
static TBOOL s_bUserDynamicLightsEnabled       = TTRUE;
static TBOOL s_bUserDynamicLightShadowsEnabled = TTRUE;

struct ToggleEntry
{
	const TCHAR* szName;
	TBOOL*       pValue;
};

static const ToggleEntry s_aToggles[] = {
	{ "csm", &s_bUserCSMEnabled },
	{ "cloudShadows", &s_bUserCloudShadowsEnabled },
	{ "ao", &s_bUserAOEnabled },
	{ "ssr", &s_bUserSSREnabled },
	{ "hdrBloom", &s_bUserHDRBloomEnabled },
	{ "sunShafts", &s_bUserSunShaftsEnabled },
	{ "volumetricFog", &s_bUserVolumetricFogEnabled },
	{ "dynamicLights", &s_bUserDynamicLightsEnabled },
	{ "dynamicLightShadows", &s_bUserDynamicLightShadowsEnabled },
};

void GameSettings::Load( const TCHAR* a_szPath )
{
	s_bHasDisplay = TFALSE;

	tinyxml2::XMLDocument oDoc;
	if ( oDoc.LoadFile( a_szPath ) != tinyxml2::XML_SUCCESS )
		return;

	const tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "gameSettings" );
	if ( !pRoot )
		return;

	if ( const tinyxml2::XMLElement* pDisplay = pRoot->FirstChildElement( "display" ) )
	{
		s_oDisplay.uiWidth       = pDisplay->UnsignedAttribute( "width", 0 );
		s_oDisplay.uiHeight      = pDisplay->UnsignedAttribute( "height", 0 );
		s_oDisplay.eDisplayMode  = DisplayMode( pDisplay->UnsignedAttribute( "displayMode", DISPLAY_WINDOWED ) );
		s_oDisplay.bVSync        = pDisplay->BoolAttribute( "vsync", TFALSE ) ? TTRUE : TFALSE;
		s_oDisplay.uiMSAASamples = pDisplay->UnsignedAttribute( "msaa", 1 );
		s_oDisplay.eCSMPreset    = CSMPreset( pDisplay->UnsignedAttribute( "shadowPreset", CSM_PRESET_MEDIUM ) );
		s_oDisplay.eAAMode       = AAMode( pDisplay->UnsignedAttribute( "postAA", AA_NONE ) );

		if ( s_oDisplay.eDisplayMode > DISPLAY_FULLSCREEN )
			s_oDisplay.eDisplayMode = DISPLAY_WINDOWED;
		if ( s_oDisplay.eCSMPreset >= CSM_PRESET_COUNT )
			s_oDisplay.eCSMPreset = CSM_PRESET_MEDIUM;
		if ( s_oDisplay.eAAMode > AA_SMAA )
			s_oDisplay.eAAMode = AA_NONE;

		s_bHasDisplay = ( s_oDisplay.uiWidth != 0 && s_oDisplay.uiHeight != 0 );
	}

	if ( const tinyxml2::XMLElement* pEffects = pRoot->FirstChildElement( "effects" ) )
	{
		for ( TINT i = 0; i < TARRAYSIZE( s_aToggles ); i++ )
			*s_aToggles[ i ].pValue = pEffects->BoolAttribute( s_aToggles[ i ].szName, (TBOOL)*s_aToggles[ i ].pValue ) ? TTRUE : TFALSE;
	}
}

void GameSettings::Save( const TCHAR* a_szPath, const GraphicsSettings& a_rcDisplay )
{
	tinyxml2::XMLDocument oDoc;
	tinyxml2::XMLElement* pRoot = oDoc.NewElement( "gameSettings" );
	oDoc.InsertFirstChild( pRoot );

	tinyxml2::XMLElement* pDisplay = oDoc.NewElement( "display" );
	pDisplay->SetAttribute( "width", a_rcDisplay.uiWidth );
	pDisplay->SetAttribute( "height", a_rcDisplay.uiHeight );
	pDisplay->SetAttribute( "displayMode", TUINT( a_rcDisplay.eDisplayMode ) );
	pDisplay->SetAttribute( "vsync", (TBOOL)a_rcDisplay.bVSync );
	pDisplay->SetAttribute( "msaa", a_rcDisplay.uiMSAASamples );
	pDisplay->SetAttribute( "shadowPreset", TUINT( a_rcDisplay.eCSMPreset ) );
	pDisplay->SetAttribute( "postAA", TUINT( a_rcDisplay.eAAMode ) );
	pRoot->InsertEndChild( pDisplay );

	tinyxml2::XMLElement* pEffects = oDoc.NewElement( "effects" );
	for ( TINT i = 0; i < TARRAYSIZE( s_aToggles ); i++ )
		pEffects->SetAttribute( s_aToggles[ i ].szName, (TBOOL)*s_aToggles[ i ].pValue );
	pRoot->InsertEndChild( pEffects );

	oDoc.SaveFile( a_szPath );
}

TBOOL GameSettings::HasDisplaySettings()
{
	return s_bHasDisplay;
}

const GraphicsSettings& GameSettings::GetDisplaySettings()
{
	return s_oDisplay;
}

TBOOL GameSettings::IsCSMEnabled() { return g_bCSMEnabled && s_bUserCSMEnabled; }
TBOOL GameSettings::AreCloudShadowsEnabled() { return g_bCloudShadowsEnabled && s_bUserCloudShadowsEnabled; }
TBOOL GameSettings::IsAOEnabled() { return g_bHBAOEnabled && s_bUserAOEnabled; }
TBOOL GameSettings::IsSSREnabled() { return g_bSSREnabled && s_bUserSSREnabled; }
TBOOL GameSettings::IsSkyCubeEnabled() { return g_bSkyCubeEnabled; }
TBOOL GameSettings::IsEnvSpecularEnabled() { return g_bEnvSpecular; }
TBOOL GameSettings::IsGlowBloomEnabled() { return g_bGlowBloomEnabled; }
TBOOL GameSettings::IsHDRBloomEnabled() { return g_bHDRBloomEnabled && s_bUserHDRBloomEnabled; }
TBOOL GameSettings::AreSunShaftsEnabled() { return g_bSunShaftsEnabled && s_bUserSunShaftsEnabled; }
TBOOL GameSettings::IsVolumetricFogEnabled() { return g_bVolumetricFogEnabled && s_bUserVolumetricFogEnabled; }
TBOOL GameSettings::AreDynamicLightsEnabled() { return g_bDynamicLightEnabled && s_bUserDynamicLightsEnabled; }
TBOOL GameSettings::AreDynamicLightShadowsEnabled() { return g_bDynamicLightShadowsEnabled && s_bUserDynamicLightShadowsEnabled; }
TBOOL GameSettings::IsWindEnabled() { return g_bWindEnabled; }

TBOOL GameSettings::g_bRenderUI = TFALSE;

static GraphicsSettings s_oWorking;

static const TUINT s_aMSAASamples[] = { 1, 2, 4, 8 };

void GameSettings::Render()
{
	static TBOOL s_bWasEnabled = TFALSE;
	const TBOOL  bJustOpened   = ( g_bRenderUI && !s_bWasEnabled );
	s_bWasEnabled              = g_bRenderUI;

	if ( !g_bRenderUI )
		return;

	RenderDX11* pRender = remaster::g_pRender;
	if ( !pRender )
		return;

	if ( bJustOpened )
		s_oWorking = pRender->GetGraphicsSettings();

	ImGui::SetNextWindowSize( ImVec2( 360.0f, 0.0f ), ImGuiCond_FirstUseEver );
	ImGui::Begin( "Graphics Settings" );

	{
		ImGui::Text( "Surface: %.0f x %.0f", pRender->GetSurfaceWidth(), pRender->GetSurfaceHeight() );

		if ( AGUI2::IsSingletonCreated() && AGUI2::GetContext() )
		{
			TFLOAT flCanvasW = 0.0f, flCanvasH = 0.0f;
			AGUI2::GetContext()->GetRootElement()->GetDimensions( flCanvasW, flCanvasH );
			ImGui::Text( "UI Canvas: %.0f x %.0f", flCanvasW, flCanvasH );
		}
		ImGui::Separator();
	}

	// Resolution
	{
		const Toshi::T2DynamicVector<RenderDX11::Resolution>& rcResolutions = pRender->GetAvailableResolutions();

		TINT iResIndex = -1;
		for ( TINT i = 0; i < rcResolutions.Size(); i++ )
		{
			if ( rcResolutions[ i ].uiWidth == s_oWorking.uiWidth && rcResolutions[ i ].uiHeight == s_oWorking.uiHeight )
			{
				iResIndex = i;
				break;
			}
		}

		TCHAR szPreview[ 32 ];
		T2String8::Format( szPreview, sizeof( szPreview ), "%u x %u", s_oWorking.uiWidth, s_oWorking.uiHeight );

		const TBOOL bResolutionLocked = ( s_oWorking.eDisplayMode == DISPLAY_BORDERLESS );
		if ( bResolutionLocked )
			ImGui::BeginDisabled(); // borderless always uses the native desktop resolution

		if ( ImGui::BeginCombo( "Resolution", szPreview ) )
		{
			for ( TINT i = 0; i < rcResolutions.Size(); i++ )
			{
				TCHAR szLabel[ 32 ];
				T2String8::Format( szLabel, sizeof( szLabel ), "%u x %u", rcResolutions[ i ].uiWidth, rcResolutions[ i ].uiHeight );

				const TBOOL bSelected = ( i == iResIndex );
				if ( ImGui::Selectable( szLabel, bSelected ) )
				{
					s_oWorking.uiWidth  = rcResolutions[ i ].uiWidth;
					s_oWorking.uiHeight = rcResolutions[ i ].uiHeight;
				}
				if ( bSelected )
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if ( bResolutionLocked )
			ImGui::EndDisabled();
	}

	// Display Mode
	{
		static const TCHAR* apModes[] = { "Windowed", "Borderless", "Fullscreen" };
		TINT                iMode     = TINT( s_oWorking.eDisplayMode );
		if ( ImGui::Combo( "Display Mode", &iMode, apModes, TARRAYSIZE( apModes ) ) )
			s_oWorking.eDisplayMode = DisplayMode( iMode );
	}

	// VSYNC
	{
		TBOOL bVSync = (TBOOL)s_oWorking.bVSync;
		if ( ImGui::Checkbox( "VSync", &bVSync ) )
			s_oWorking.bVSync = (TBOOL)bVSync;
	}

	// MSAA
	{
		static const TCHAR* apMSAA[] = { "Off", "2x", "4x", "8x" };
		TINT                iMSAA    = 0;
		for ( TINT i = 0; i < TARRAYSIZE( s_aMSAASamples ); i++ )
		{
			if ( s_aMSAASamples[ i ] == s_oWorking.uiMSAASamples )
			{
				iMSAA = i;
				break;
			}
		}
		if ( ImGui::Combo( "Anti-Aliasing", &iMSAA, apMSAA, TARRAYSIZE( apMSAA ) ) )
			s_oWorking.uiMSAASamples = s_aMSAASamples[ iMSAA ];
	}

	// FXAA/SMAA
	{
		static const TCHAR* apPostAA[] = { "Off", "FXAA", "SMAA" };
		TINT                iPostAA    = TINT( s_oWorking.eAAMode );
		if ( ImGui::Combo( "Post-Processing AA", &iPostAA, apPostAA, TARRAYSIZE( apPostAA ) ) )
			s_oWorking.eAAMode = AAMode( iPostAA );
	}

	// CSM Quality
	{
		static const TCHAR* apCSM[] = { "Low (1024)", "Medium (2048)", "High (4096)" };
		TINT                iCSM    = TINT( s_oWorking.eCSMPreset );
		if ( ImGui::Combo( "Shadow Resolution", &iCSM, apCSM, TARRAYSIZE( apCSM ) ) )
			s_oWorking.eCSMPreset = CSMPreset( iCSM );
	}

	// Effects (take effect immediately and are persisted right away)
	{
		ImGui::Separator();
		ImGui::Text( "Effects" );

		// User overrides on top of the per-level artistic values; unchecking forces an
		// effect off everywhere, checking defers to what the level authored
		TBOOL bChanged = TFALSE;

#define EFFECT_TOGGLE( label, name )                     \
	{                                                    \
		TBOOL bValue = s_bUser##name;                    \
		if ( ImGui::Checkbox( label, &bValue ) )         \
		{                                                \
			s_bUser##name = ( bValue ? TTRUE : TFALSE ); \
			bChanged      = TTRUE;                       \
		}                                                \
	}


		EFFECT_TOGGLE( "Shadows (CSM)", CSMEnabled );
		EFFECT_TOGGLE( "Cloud Shadows", CloudShadowsEnabled );
		EFFECT_TOGGLE( "Ambient Occlusion", AOEnabled );
		EFFECT_TOGGLE( "Screen-Space Reflections", SSREnabled );
		EFFECT_TOGGLE( "HDR Bloom", HDRBloomEnabled );
		EFFECT_TOGGLE( "Sun Shafts", SunShaftsEnabled );
		EFFECT_TOGGLE( "Volumetric Fog", VolumetricFogEnabled );
		EFFECT_TOGGLE( "Dynamic Lights", DynamicLightsEnabled );
		EFFECT_TOGGLE( "Dynamic Light Shadows", DynamicLightShadowsEnabled );

#undef EFFECT_TOGGLE

		if ( bChanged )
			Save( "Data\\GameSettings.xml", pRender->GetGraphicsSettings() );
	}

	ImGui::Separator();

	if ( ImGui::Button( "Apply" ) )
	{
		pRender->RequestDisplayMode( s_oWorking.eDisplayMode );
		pRender->RequestResolution( s_oWorking.uiWidth, s_oWorking.uiHeight );
		pRender->RequestVSync( s_oWorking.bVSync );
		pRender->RequestMSAA( s_oWorking.uiMSAASamples );
		pRender->RequestCSMPreset( s_oWorking.eCSMPreset );
		pRender->RequestAAMode( s_oWorking.eAAMode );

		Save( "Data\\GameSettings.xml", s_oWorking );
	}

	ImGui::SameLine();
	if ( ImGui::Button( "Revert" ) )
		s_oWorking = pRender->GetGraphicsSettings();

	ImGui::End();
}
