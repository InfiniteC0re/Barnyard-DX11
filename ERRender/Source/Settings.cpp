#include "pch.h"
#include "Settings.h"
#include "RenderDX11.h"

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

namespace settings
{

bool g_bEnabled = TFALSE;

static GraphicsSettings s_oWorking;

static const TUINT s_aMSAASamples[] = { 1, 2, 4, 8 };

void Render()
{
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

		const bool bResolutionLocked = ( s_oWorking.eDisplayMode == DISPLAY_BORDERLESS );
		if ( bResolutionLocked )
			ImGui::BeginDisabled(); // borderless always uses the native desktop resolution

		if ( ImGui::BeginCombo( "Resolution", szPreview ) )
		{
			for ( TINT i = 0; i < rcResolutions.Size(); i++ )
			{
				TCHAR szLabel[ 32 ];
				T2String8::Format( szLabel, sizeof( szLabel ), "%u x %u", rcResolutions[ i ].uiWidth, rcResolutions[ i ].uiHeight );

				const bool bSelected = ( i == iResIndex );
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
		static const char* apModes[] = { "Windowed", "Borderless", "Fullscreen" };
		TINT               iMode     = TINT( s_oWorking.eDisplayMode );
		if ( ImGui::Combo( "Display Mode", &iMode, apModes, TARRAYSIZE( apModes ) ) )
			s_oWorking.eDisplayMode = DisplayMode( iMode );
	}

	// VSYNC
	{
		bool bVSync = (bool)s_oWorking.bVSync;
		if ( ImGui::Checkbox( "VSync", &bVSync ) )
			s_oWorking.bVSync = (TBOOL)bVSync;
	}

	// MSAA
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

	// FXAA/SMAA
	{
		static const char* apPostAA[] = { "Off", "FXAA", "SMAA" };
		TINT               iPostAA    = TINT( s_oWorking.eAAMode );
		if ( ImGui::Combo( "Post-Processing AA", &iPostAA, apPostAA, TARRAYSIZE( apPostAA ) ) )
			s_oWorking.eAAMode = AAMode( iPostAA );
	}

	// CSM Quality
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
		pRender->RequestAAMode( s_oWorking.eAAMode );
	}

	ImGui::SameLine();
	if ( ImGui::Button( "Revert" ) )
		s_oWorking = pRender->GetGraphicsSettings();

	ImGui::End();
}

} // namespace settings
