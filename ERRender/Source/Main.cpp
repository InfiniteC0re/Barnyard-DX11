#include "pch.h"

#include "RenderDX11.h"
#include "RenderContentDX11.h"
#include "LightManager.h"
#include "MaterialParams.h"
#include "Editor.h"
#include "GameSettings.h"
#include "CSM/CSMManager.h"

#include <StaticLights.h>

#include "UI/FontRenderer.h"
#include "UI/Rml/RmlManager.h"
#include "UI/Rml/ARmlVideoSettingsState.h"
#include "UI/Rml/ARmlFrontEndState.h"

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

namespace remaster
{

const T2CommandLine* g_pCommandLine = TNULL;

} // namespace remaster

static void Bridge_GatherStaticLights( const TSphere& a_rcBounds, TINT8* a_pOutIDs )
{
	if ( remaster::g_pLightManager )
		remaster::g_pLightManager->GetInfluencingStaticLightIDs( a_rcBounds, a_pOutIDs );
	else
		for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
			a_pOutIDs[ i ] = -1;
}

static void Bridge_AddStaticLights( TRenderContext* a_pContext, const TINT8* a_pIDs )
{
	auto pContext = TSTATICCAST( remaster::RenderContextD3D11, a_pContext );
	for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
	{
		if ( a_pIDs[ i ] < 0 )
			break;
		pContext->AddStaticLight( a_pIDs[ i ] );
	}
}

static void Bridge_ClearStaticLights( TRenderContext* a_pContext )
{
	TSTATICCAST( remaster::RenderContextD3D11, a_pContext )->ClearStaticLightIDs();
}

class ERRenderMod : public AModInstance
{
	TBOOL m_bDebugFontAtlas = TFALSE;

public:
	TBOOL OnLoad() OVERRIDE
	{
		editor::SetupHooks();
		remaster::SetupRenderer();

		SetStaticLightCallbacks( Bridge_GatherStaticLights, Bridge_AddStaticLights, Bridge_ClearStaticLights );

		return TTRUE;
	}

	TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE
	{
		g_oSystemManager.Update();

		return TTRUE;
	}

	void OnUnload() OVERRIDE
	{
		remaster::rml::Shutdown();
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

		if ( ImGui::Button( "Reload Materials" ) )
			remaster::ReloadMaterialParams();

		ImGui::Separator();
		ImGui::TextDisabled( "Render settings moved to the Level Settings window (Alt+Z), Settings tab." );
	}

	virtual void OnImGuiRenderOverlay( AImGUI* a_pImGui )
	{
		if ( editor::g_bEnabled ) editor::Render();

		remaster::GameSettings::Render();

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
		return ( editor::g_bEnabled || remaster::GameSettings::g_bRenderUI || m_bDebugFontAtlas );
	}
};

extern "C"
{
	MODLOADER_EXPORT AModInstance* CreateModInstance( const T2CommandLine* a_pCommandLine )
	{
		TMemory::Initialise( 32 * 1024 * 1024, 0 );

		TUtil::TOSHIParams toshiParams;
		toshiParams.szCommandLine = "";
		toshiParams.szLogFileName = "er-render";
		toshiParams.szLogAppName  = "ERRender";

		TUtil::ToshiCreate( toshiParams );

		// Override bike light pos
		*(TUINT32*)( 0x007838bc ) |= 1;
		*(TVector4*)( 0x007838ac ) = TVector4::VEC_ZERO;

		remaster::g_pCommandLine = a_pCommandLine;
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
