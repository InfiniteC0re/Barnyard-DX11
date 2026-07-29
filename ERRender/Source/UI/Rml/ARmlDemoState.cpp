#include "pch.h"
#include "ARmlDemoState.h"
#include "RmlManager.h"

#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/AGUI2.h>
#include <BYardSDK/ARootTask.h>

#include <Input/TInputDeviceKeyboard.h>
#include <Toshi/T2String8.h>

// windowsx.h (via pch) defines these as macros that collide with Rml names
#undef GetFirstChild
#undef GetNextSibling
#undef GetPrevSibling
#undef GetNextWindow

#include <RmlUi/Core.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( ARmlDemoState );

ARmlDemoState::ARmlDemoState()
{
	m_pDocument = TNULL;
	m_iClicks   = 0;
}

void ARmlDemoState::OnActivate()
{
	remaster::rml::SetEnabled( TTRUE );

	// Data model must exist before the document that binds to it is loaded
	m_iClicks = 0;
	if ( Rml::Context* pContext = remaster::rml::GetContext() )
	{
		Rml::DataModelConstructor oModel = pContext->CreateDataModel( "demo" );
		oModel.Bind( "clicks", &m_iClicks );
		oModel.BindEventCallback(
		    "add",
		    [ this ]( Rml::DataModelHandle a_hModel, Rml::Event&, const Rml::VariantList& )
		    {
			    m_iClicks++;
			    a_hModel.DirtyVariable( "clicks" );
		    }
		);
	}

	TCHAR szRml[ 1536 ];
	T2String8::Format(
	    szRml, sizeof( szRml ),
	    "<rml><head><style>"
	    "body { font-family: %s; color: white; }"
	    "#panel { position: absolute; top: 40dp; left: 40dp; width: 320dp; padding: 20dp;"
	    "background-color: #1e2f4dcc; border: 2dp #6fa8ff; border-radius: 8dp;"
	    "transform: rotate(-2deg); }"
	    "h1 { font-size: 16dp; color: #9ecbff; margin-bottom: 8dp; }"
	    "p { font-size: 12dp; margin-bottom: 12dp; }"
	    ".btn { display: block; width: 200dp; padding: 8dp 12dp; text-align: center;"
	    "background-color: #2a63b8; border-radius: 6dp;"
	    "transition: transform 0.15s, background-color 0.15s; }"
	    ".btn:hover { background-color: #3f86e6; transform: scale(1.06); }"
	    ".btn:active { background-color: #9ecbff; color: #10203a; }"
	    "</style></head><body data-model='demo'>"
	    "<div id='panel'><h1>RmlUi Demo</h1>"
	    "<p>Clicks: {{clicks}}</p>"
	    "<div class='btn' data-event-click='add'>Add one</div>"
	    "<p>Press Escape to close.</p>"
	    "</div></body></rml>",
	    remaster::rml::GetFontFamily()
	);

	m_pDocument = remaster::rml::LoadDocumentFromMemory( szRml );
	remaster::rml::ShowDocument( m_pDocument );

	// Show the game cursor and freeze gameplay while the modal is up
	AGUI2::GetSingleton()->m_bShowMouseCursor = TTRUE;
	ARootTask::GetSingleton()->SetPaused( TTRUE );
}

void ARmlDemoState::OnDeactivate()
{
	ARootTask::GetSingleton()->SetPaused( TFALSE );
	AGUI2::GetSingleton()->m_bShowMouseCursor = TFALSE;

	remaster::rml::CloseDocument( m_pDocument );
	m_pDocument = TNULL;

	if ( Rml::Context* pContext = remaster::rml::GetContext() )
		pContext->RemoveDataModel( "demo" );

	remaster::rml::SetEnabled( TFALSE );
}

TBOOL ARmlDemoState::OnUpdate( TFLOAT a_fDeltaTime )
{
	return TTRUE;
}

TBOOL ARmlDemoState::ProcessInput( const TInputInterface::InputEvent* a_pInputEvent )
{
	if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN &&
	     a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_ESCAPE )
	{
		AGameStateController::GetSingleton()->PopCurrentGameState();
	}

	// Modal: swallow input so gameplay under the menu stays idle
	return TTRUE;
}

static void ToggleDemoState()
{
	AGameStateController* pController = AGameStateController::GetSingleton();

	if ( pController->IsCurrentState( &TGetClass( ARmlDemoState ) ) )
		pController->PopCurrentGameState();
	else
		pController->PushState( new ARmlDemoState() );
}

void SetupRmlDemoState()
{
	remaster::rml::SetDebugToggleHandler( ToggleDemoState );
}
