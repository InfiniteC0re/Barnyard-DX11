#include "pch.h"

#include <AImGUI.h>
#include <ModLoader.h>
#include <AHooks.h>
#include <HookHelpers.h>

#include <BYardSDK/AGUI2.h>
#include <BYardSDK/THookedRenderD3DInterface.h>
#include <BYardSDK/AGUI2FontManager.h>
#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/ATerrainInterface.h>
#include <BYardSDK/ACamera.h>
#include <BYardSDK/ASteer.h>
#include <BYardSDK/APlayerManager.h>

#include <Toshi/THPTimer.h>
#include <Toshi/TScheduler.h>
#include <File/TFile.h>
#include <ToshiTools/T2CommandLine.h>

TOSHI_NAMESPACE_USING

class ERRenderMod : public AModInstance
{
public:
	TBOOL OnLoad() OVERRIDE
	{
		return TTRUE;
	}

	TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE
	{
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
	}

	virtual void OnImGuiRenderOverlay( AImGUI* a_pImGui )
	{
	}

	TBOOL HasSettingsUI() OVERRIDE
	{
		return TFALSE;
	}

	virtual TBOOL IsOverlayVisible() OVERRIDE
	{
		return TFALSE;
	}
};

extern "C"
{
	MODLOADER_EXPORT AModInstance* CreateModInstance( const T2CommandLine* a_pCommandLine )
	{
		// TODO: Specify max memory size to allocate for the mod
		TMemory::Initialise( 1 * 1024 * 1024, 0 );

		TUtil::TOSHIParams toshiParams;
		toshiParams.szCommandLine = "";
		toshiParams.szLogFileName = "er-render";
		toshiParams.szLogAppName  = "RERender";

		TUtil::ToshiCreate( toshiParams );

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
