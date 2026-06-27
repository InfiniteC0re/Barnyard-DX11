#include "pch.h"
#include "Settings.h"
#include "RenderDX11.h"

#include <BYardSDK/AGUI2.h>

#include <imgui.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

using namespace remaster;

namespace settings
{

bool g_bEnabled = TFALSE;

// Working copy edited by the UI; pushed to the renderer only when "Apply" is pressed.
// Re-synced from the live (active) settings each time the window is (re)opened.
static GraphicsSettings s_oWorking;

// Common windowed resolutions offered by the resolution combo.
struct ResolutionOption
{
	TUINT       uiWidth;
	TUINT       uiHeight;
	const char* szLabel;
};

static const ResolutionOption s_aResolutions[] = {
	{ 1280, 720,  "1280 x 720"  },
	{ 1366, 768,  "1366 x 768"  },
	{ 1600, 900,  "1600 x 900"  },
	{ 1920, 1080, "1920 x 1080" },
	{ 2560, 1440, "2560 x 1440" },
	{ 3840, 2160, "3840 x 2160" },
};

static const TUINT s_aMSAASamples[] = { 1, 2, 4, 8 };

// ============================================================
// Render (called from OnImGuiRenderOverlay)
// ============================================================

void Render()
{
	// Re-sync the working copy from the live settings on the rising edge of g_bEnabled
	// (i.e. each time the window is opened) so it always reflects what is actually active.
	static bool s_bWasEnabled = false;
	const bool  bJustOpened    = ( g_bEnabled && !s_bWasEnabled );
	s_bWasEnabled              = g_bEnabled;

	if ( !g_bEnabled )
		return;

	RenderDX11* pRender = remaster::g_pRender;
	if ( !pRender )
		return;

	if ( bJustOpened )
		s_oWorking = pRender->GetGraphicsSettings();

	ImGui::SetNextWindowSize( ImVec2( 360.0f, 0.0f ), ImGuiCond_FirstUseEver );
	ImGui::Begin( "Graphics Settings" );

	// --- Live diagnostics ---------------------------------------------------
	// Shows the actual backbuffer size and the AGUI2 virtual canvas. For a same-aspect
	// resolution change the canvas must stay constant (e.g. 936x702 for all 16:9); if it
	// changes here, the UI canvas selection is the cause of any layout shift.
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

	// --- Resolution ---------------------------------------------------------
	{
		TINT iResIndex = -1;
		for ( TINT i = 0; i < TARRAYSIZE( s_aResolutions ); i++ )
		{
			if ( s_aResolutions[ i ].uiWidth == s_oWorking.uiWidth && s_aResolutions[ i ].uiHeight == s_oWorking.uiHeight )
			{
				iResIndex = i;
				break;
			}
		}

		const char* szPreview = ( iResIndex >= 0 ) ? s_aResolutions[ iResIndex ].szLabel : "(custom)";

		const bool bBorderless = ( s_oWorking.eDisplayMode != DISPLAY_WINDOWED );
		if ( bBorderless )
			ImGui::BeginDisabled(); // borderless/fullscreen tracks the desktop resolution

		if ( ImGui::BeginCombo( "Resolution", szPreview ) )
		{
			for ( TINT i = 0; i < TARRAYSIZE( s_aResolutions ); i++ )
			{
				const bool bSelected = ( i == iResIndex );
				if ( ImGui::Selectable( s_aResolutions[ i ].szLabel, bSelected ) )
				{
					s_oWorking.uiWidth  = s_aResolutions[ i ].uiWidth;
					s_oWorking.uiHeight = s_aResolutions[ i ].uiHeight;
				}
				if ( bSelected )
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if ( bBorderless )
			ImGui::EndDisabled();
	}

	// --- Display mode -------------------------------------------------------
	{
		static const char* apModes[] = { "Windowed", "Borderless", "Fullscreen" };
		TINT               iMode     = TINT( s_oWorking.eDisplayMode );
		if ( ImGui::Combo( "Display Mode", &iMode, apModes, TARRAYSIZE( apModes ) ) )
			s_oWorking.eDisplayMode = DisplayMode( iMode );
	}

	// --- VSync --------------------------------------------------------------
	{
		bool bVSync = (bool)s_oWorking.bVSync;
		if ( ImGui::Checkbox( "VSync", &bVSync ) )
			s_oWorking.bVSync = (TBOOL)bVSync;
	}

	// --- MSAA ---------------------------------------------------------------
	{
		static const char* apMSAA[] = { "Off", "2x", "4x", "8x" };
		TINT               iMSAA    = 0;
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

	// --- CSM shadow resolution ---------------------------------------------
	{
		static const char* apCSM[] = { "Low (1024)", "Medium (2048)", "High (4096)" };
		TINT               iCSM    = TINT( s_oWorking.eCSMPreset );
		if ( ImGui::Combo( "Shadow Resolution", &iCSM, apCSM, TARRAYSIZE( apCSM ) ) )
			s_oWorking.eCSMPreset = CSMPreset( iCSM );
	}

	ImGui::Separator();

	if ( ImGui::Button( "Apply" ) )
	{
		pRender->RequestDisplayMode( s_oWorking.eDisplayMode );
		pRender->RequestResolution( s_oWorking.uiWidth, s_oWorking.uiHeight );
		pRender->RequestVSync( s_oWorking.bVSync );
		pRender->RequestMSAA( s_oWorking.uiMSAASamples );
		pRender->RequestCSMPreset( s_oWorking.eCSMPreset );
	}

	ImGui::SameLine();
	if ( ImGui::Button( "Revert" ) )
		s_oWorking = pRender->GetGraphicsSettings();

	// -----------------------------------------------------------------------
	// Future: game-texture preview panel. Bind any game ID3D11ShaderResourceView
	// and display it here, e.g.:
	//   ImGui::Image( (ImTextureID)pSomeGameSRV, ImVec2( 256.0f, 256.0f ) );
	// (see the font-atlas preview in Main.cpp's OnImGuiRenderOverlay for the pattern)
	// -----------------------------------------------------------------------

	ImGui::End();
}

} // namespace settings
