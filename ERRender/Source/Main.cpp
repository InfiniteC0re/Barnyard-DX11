#include "pch.h"

#include "RenderDX11.h"
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

class ERRenderMod : public AModInstance
{
	TBOOL m_bDebugFontAtlas = TFALSE;

public:
	TBOOL OnLoad() OVERRIDE
	{
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
	}

	virtual void OnImGuiRenderOverlay( AImGUI* a_pImGui )
	{
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
		return TTRUE;
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
