#include "pch.h"

#include "RenderDX11.h"
#include "Editor.h"
#include "CSM/CSMManager.h"

#include "UI/FontRenderer.h"

#include <AImGUI.h>
#include <ModLoader.h>
#include <AHooks.h>
#include <HookHelpers.h>

#include <BYardSDK/AGUI2.h>
#include <BYardSDK/THookedRenderD3DInterface.h>

#include <Toshi/THPTimer.h>
#include <Toshi/TScheduler.h>
#include <File/TFile.h>
#include <ToshiTools/T2CommandLine.h>

TOSHI_NAMESPACE_USING

static constexpr TINT KAWASE_MAX_LEVELS = 5;

namespace remaster
{

extern TBOOL  g_bSunShaftsEnabled;
extern TFLOAT g_flSunShaftsAlpha;
extern TFLOAT g_flSunShaftsRaysLength;
extern TFLOAT g_flSunShaftsTint[ 3 ];
extern TINT   g_iSunShaftsKawaseLevels;
extern TFLOAT g_flSunShaftsKawaseOffset;
extern TBOOL  g_bGlowBloomEnabled;
extern TINT   g_iGlowBloomKawaseLevels;
extern TFLOAT g_flGlowBloomKawaseOffset;
extern TFLOAT g_flGlowBloomIntensity;
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
extern TBOOL  g_bHBAOEnabled;
extern TBOOL  g_bHBAODebug;
extern TINT   g_iAOAlgorithm;
extern TFLOAT g_flHBAORadius;
extern TFLOAT g_flHBAOSceneScale;
extern TFLOAT g_flHBAOBias;
extern TFLOAT g_flHBAOIntensity;
extern TFLOAT g_flHBAOPower;
extern TFLOAT g_flHBAOBlurSharpness;
extern TFLOAT g_flXeGTAORadiusMultiplier;
extern TFLOAT g_flXeGTAOFalloffRange;
extern TFLOAT g_flXeGTAOSampleDistributionPower;
extern TFLOAT g_flXeGTAOThinOccluderCompensation;
extern TBOOL  g_bVolumetricFogEnabled;
extern TINT   g_iVolumetricFogCompositeMode;
extern TFLOAT g_flVolumetricFogDensity;
extern TFLOAT g_flVolumetricFogG;
extern TFLOAT g_flVolumetricFogMaxDist;
extern TFLOAT g_flVolumetricFogIntensity;
extern TFLOAT g_flVolumetricFogColor[ 3 ];

} // namespace remaster

class ERRenderMod : public AModInstance
{
	TBOOL m_bDebugFontAtlas = TFALSE;

public:
	TBOOL OnLoad() OVERRIDE
	{
		editor::SetupHooks();
		remaster::SetupRenderHooks();

		return TTRUE;
	}

	TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE
	{
		g_oSystemManager.Update();

		return TTRUE;
	}

	void OnUnload() OVERRIDE
	{
	}

	void OnRenderInterfaceReady( Toshi::TRenderD3DInterface* a_pRenderInterface ) OVERRIDE
	{
		TRenderInterface::SetSingletonExplicit(
		    THookedRenderD3DInterface::GetSingleton()
		);
	}

	void OnAGUI2Ready() OVERRIDE
	{
	}

	void OnImGuiRender( AImGUI* a_pImGui ) OVERRIDE
	{
		ImGui::Checkbox( "Enabled Font Atlas Debugging", &m_bDebugFontAtlas );

		ImGui::Separator();
		ImGui::TextUnformatted( "CSM Debugging" );

		const char* apCascadeModes[] = {
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

		ImGui::Separator();
		ImGui::TextUnformatted( "Sun Direction" );
		ImGui::Checkbox( "Override Sun Direction", &remaster::g_bOverrideSunDirection );
		if ( remaster::g_bOverrideSunDirection )
		{
			ImGui::DragFloat( "Azimuth", &remaster::g_flSunAzimuth, 0.5f, 0.0f, 0.0f, "%.2f deg" );
			ImGui::DragFloat( "Elevation", &remaster::g_flSunElevation, 0.5f, 0.0f, 0.0f, "%.2f deg" );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Shadow Settings" );
		ImGui::Checkbox( "Enable CSM", &remaster::g_bCSMEnabled );
		ImGui::SliderFloat( "Shadow Intensity", &remaster::g_flShadowIntensity, 0.0f, 1.0f );
		ImGui::DragFloat( "Shadow Distance", &remaster::g_flShadowDistance, 1.0f, 10.0f, 500.0f, "%.0f m" );
		ImGui::DragFloat( "Cascade Padding", &remaster::g_flShadowCascadePadding, 0.25f, 0.0f, 50.0f, "%.1f m" );
		ImGui::DragFloat( "Caster Padding", &remaster::g_flShadowCasterPadding, 1.0f, 0.0f, 300.0f, "%.0f m" );
		ImGui::DragFloat( "Slope Depth Bias", &remaster::g_flShadowSlopeScaledDepthBias, 0.01f, 0.0f, 5.0f, "%.2f" );
		ImGui::DragFloat( "Min Slope Depth Bias", &remaster::g_flShadowMinSlopeScaledDepthBias, 0.01f, 0.0f, 5.0f, "%.2f" );
		ImGui::DragFloat( "Receiver Bias", &remaster::g_flShadowReceiverBias, 0.00005f, 0.0f, 0.01f, "%.5f" );
		ImGui::SliderFloat( "Receiver Plane Bias", &remaster::g_flShadowReceiverPlaneBias, 0.0f, 2.0f, "%.2f" );
		ImGui::SliderFloat( "PCF Radius", &remaster::g_flShadowPCFRadius, 1.0f, 3.0f, "%.0f" );

		ImGui::Separator();
		ImGui::TextUnformatted( "Screen-Space AO" );
		ImGui::Checkbox( "Enable AO", &remaster::g_bHBAOEnabled );
		if ( remaster::g_bHBAOEnabled )
		{
			const char* apAOAlgorithms[] = {
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

		ImGui::Separator();
		ImGui::TextUnformatted( "Sun Shafts" );
		ImGui::Checkbox( "Enable Sun Shafts", &remaster::g_bSunShaftsEnabled );
		if ( remaster::g_bSunShaftsEnabled )
		{
			ImGui::SliderFloat( "Intensity", &remaster::g_flSunShaftsAlpha, 0.0f, 0.5f, "%.4f" );
			ImGui::SliderFloat( "Rays Length", &remaster::g_flSunShaftsRaysLength, 0.0f, 1.0f );
			ImGui::ColorEdit3( "Tint", remaster::g_flSunShaftsTint );
			ImGui::SliderInt( "Blur Levels", &remaster::g_iSunShaftsKawaseLevels, 1, KAWASE_MAX_LEVELS );
			ImGui::SliderFloat( "Blur Offset", &remaster::g_flSunShaftsKawaseOffset, 0.1f, 4.0f );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Glow Bloom" );
		ImGui::Checkbox( "Enable Glow Bloom", &remaster::g_bGlowBloomEnabled );
		if ( remaster::g_bGlowBloomEnabled )
		{
			ImGui::SliderInt( "Glow Blur Levels", &remaster::g_iGlowBloomKawaseLevels, 1, KAWASE_MAX_LEVELS );
			ImGui::SliderFloat( "Glow Blur Offset", &remaster::g_flGlowBloomKawaseOffset, 0.1f, 12.0f );
			ImGui::SliderFloat( "Glow Intensity", &remaster::g_flGlowBloomIntensity, 0.0f, 10.0f );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Dynamic Glow Lights" );
		ImGui::Checkbox( "Enable Dynamic Glow Lights", &remaster::g_bDynamicGlowEnabled );
		if ( remaster::g_bDynamicGlowEnabled )
		{
			ImGui::SliderFloat( "Glow Intensity", &remaster::g_flDynamicGlowIntensity, 0.0f, 3.0f, "%.2f" );
			ImGui::SliderFloat( "Glow Volumetric Intensity", &remaster::g_flDynamicGlowVolumetricIntensity, 0.0f, 1.0f, "%.3f" );
			ImGui::SliderFloat( "Glow Bump Scale", &remaster::g_flDynamicGlowBumpScale, 0.0f, 10.0f, "%.2f" );
			ImGui::ColorEdit3( "Glow Color", remaster::g_flDynamicGlowColor );
			ImGui::Checkbox( "Enable Dynamic Glow Shadows", &remaster::g_bDynamicGlowShadowsEnabled );
			if ( remaster::g_bDynamicGlowShadowsEnabled )
			{
				ImGui::DragFloat( "Glow Shadow Distance", &remaster::g_flDynamicGlowShadowDistance, 1.0f, 1.0f, 150.0f, "%.0f m" );
				ImGui::SliderFloat( "Glow Shadow Intensity", &remaster::g_flDynamicGlowShadowIntensity, 0.0f, 1.0f, "%.2f" );
				ImGui::DragFloat( "Glow Shadow Bias", &remaster::g_flDynamicGlowShadowBias, 0.0001f, 0.0f, 0.02f, "%.4f" );
			}
			ImGui::Checkbox( "Enable Flicker", &remaster::g_bDynamicGlowFlickerEnabled );
			if ( remaster::g_bDynamicGlowFlickerEnabled )
			{
				ImGui::SliderFloat( "Flicker Speed", &remaster::g_flDynamicGlowFlickerSpeed, 0.5f, 30.0f, "%.1f" );
				ImGui::SliderFloat( "Flicker Strength", &remaster::g_flDynamicGlowFlickerStrength, 0.0f, 1.0f, "%.2f" );
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Volumetric Fog" );
		ImGui::Checkbox( "Enable Volumetric Fog", &remaster::g_bVolumetricFogEnabled );
		if ( remaster::g_bVolumetricFogEnabled )
		{
			const char* apVolumetricFogModes[] = {
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
				ImGui::ColorEdit3( "Fog Color", remaster::g_flVolumetricFogColor );
			}
		}

	}

	virtual void OnImGuiRenderOverlay( AImGUI* a_pImGui )
	{
		if ( editor::g_bEnabled )
		{
			editor::Render();
		}

		if ( m_bDebugFontAtlas )
		{
			ImGui::SetNextWindowPos( ImVec2( 16.0f, 16.0f ), ImGuiCond_Appearing );
			ImGui::Begin(
			    "Font Atlas Debugging",
			    TNULL,
			    ImGuiWindowFlags_NoSavedSettings
			);
			{
				remaster::FontAtlas*      pFontAtlas         = remaster::g_pRender->GetFontAtlas( remaster::RenderDX11::FONT_REKORD26 );
				ID3D11ShaderResourceView* pFontAtlasResource = pFontAtlas->GetTextureResource();

				ImVec2 vContentSize = ImGui::GetContentRegionAvail();
				TFLOAT flImageSize  = vContentSize.x;
				ImGui::Image( pFontAtlasResource, ImVec2( flImageSize, flImageSize ) );

				ImGui::End();
			}
		}
	}

	TBOOL HasSettingsUI() OVERRIDE
	{
		return TTRUE;
	}

	virtual TBOOL IsOverlayVisible() OVERRIDE
	{
		return ( editor::g_bEnabled || m_bDebugFontAtlas );
	}
};

extern "C"
{
	MODLOADER_EXPORT AModInstance* CreateModInstance( const T2CommandLine* a_pCommandLine )
	{
		TMemory::Initialise( 128 * 1024 * 1024, 0 );

		TUtil::TOSHIParams toshiParams;
		toshiParams.szCommandLine = "";
		toshiParams.szLogFileName = "er-render";
		toshiParams.szLogAppName  = "ERRender";

		TUtil::ToshiCreate( toshiParams );

		remaster::fontrenderer::SetHDEnabled( !a_pCommandLine->HasParameter( "-nohdfonts" ) );

		return new ERRenderMod();
	}

	MODLOADER_EXPORT const TCHAR* GetModAutoUpdateURL()
	{
		return TNULL;
	}

	MODLOADER_EXPORT const TCHAR* GetModName()
	{
		return "ERRender";
	}

	MODLOADER_EXPORT TUINT32 GetModVersion()
	{
		return TVERSION( 1, 0 );
	}
}
