#include "pch.h"
#include "ARmlOptionsState.h"
#include "ARmlVideoSettingsState.h"
#include "RmlManager.h"
#include "RmlFocusGroup.h"

#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/AGUI2.h>
#include <BYardSDK/SDKHooks.h>
#include <BYardSDK/ASoundManager.h>

#include <Input/TInputDeviceKeyboard.h>

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

TDEFINE_CLASS( ARmlOptionsState );

namespace
{

// Single active options state, so file-static model state is fine
bool s_bModelReady = false;

// Selection is restored when returning from a pushed state (video, audio, controls)
Rml::String s_strLastFocus;

enum EPendingAction
{
	PENDING_NONE,
	PENDING_VIDEO,
	PENDING_AUDIO,
	PENDING_CONTROLS,
	PENDING_CLOSE,
};
EPendingAction s_ePending = PENDING_NONE;

static void OnVideo()    { s_ePending = PENDING_VIDEO; }
static void OnAudio()    { s_ePending = PENDING_AUDIO; }
static void OnControls() { s_ePending = PENDING_CONTROLS; }

static void PushOptionsSubState( TINT a_iButton )
{
	TBYTE aStub[ 0x0c00 ] = {};
	*( TINT* )( aStub + 0xbe0 ) = a_iButton;
	CALL_THIS( 0x00445190, void*, void, aStub );
}

} // namespace

ARmlOptionsState::ARmlOptionsState()
{
	m_pDocument = TNULL;
}

void ARmlOptionsState::OnActivate()
{
	// This instance persists on the stack while a pushed sub-state runs
	s_ePending   = PENDING_NONE;
	m_bClosing   = TFALSE;
	m_fCloseTime = 0.0f;

	remaster::rml::SetEnabled( TTRUE );
	remaster::rml::LockInput( remaster::rml::APPEAR_ANIMATION_DURATION );

	Rml::Context* pContext = remaster::rml::GetContext();
	if ( !pContext )
		return;

	if ( !s_bModelReady )
	{
		Rml::DataModelConstructor oModel = pContext->CreateDataModel( "options" );
		oModel.BindEventCallback( "video", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnVideo(); } );
		oModel.BindEventCallback( "audio", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnAudio(); } );
		oModel.BindEventCallback( "controls", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnControls(); } );
		s_bModelReady = true;
	}

	m_pDocument = remaster::rml::LoadDocument( "Data/UI/Layout/options.rml" );
	remaster::rml::ShowDocument( m_pDocument );

	// Restore the previous selection, so returning from a submenu lands where the
	// player left off; otherwise start on the first button
	remaster::RestoreFocusedElement( m_pDocument, s_strLastFocus );

	AGUI2::GetSingleton()->SetCursorVisible( TTRUE );
}

void ARmlOptionsState::OnDeactivate()
{
	AGUI2::GetSingleton()->SetCursorVisible( TFALSE );

	s_strLastFocus = remaster::GetFocusedElementId( m_pDocument );

	remaster::rml::CloseDocument( m_pDocument );
	m_pDocument = TNULL;

	remaster::rml::SetEnabled( TFALSE );
}

TBOOL ARmlOptionsState::OnUpdate( TFLOAT a_fDeltaTime )
{
	// A queued action first plays the disappear animation, then runs -- outside the
	// RmlUi click callback so the document is not torn down mid-dispatch
	if ( s_ePending != PENDING_NONE && !m_bClosing )
	{
		m_bClosing   = TTRUE;
		m_fCloseTime = 0.0f;
		remaster::rml::BeginCloseAnimation( m_pDocument, "window" );

		ASoundManager::GetSingleton()->PlayCue( 167 ); // Close sound cue
	}

	if ( m_bClosing )
	{
		m_fCloseTime += a_fDeltaTime;
		if ( m_fCloseTime >= remaster::rml::CLOSE_ANIMATION_DURATION )
		{
			m_bClosing = TFALSE;

			const EPendingAction ePending = s_ePending;
			s_ePending = PENDING_NONE;

			AGameStateController* pController = AGameStateController::GetSingleton();
			switch ( ePending )
			{
				case PENDING_VIDEO: pController->PushState( new ARmlVideoSettingsState() ); break;
				case PENDING_AUDIO: PushOptionsSubState( 0 ); break;
				case PENDING_CONTROLS: PushOptionsSubState( 1 ); break;
				case PENDING_CLOSE:
					pController->PopCurrentGameState();

					if ( *(TBOOL*)0x00781ca0 )
					{
						AGameStateController::GetSingleton()->SaveCurrentGameState( 0, TFALSE, TTRUE );
						*(TBOOL*)0x00781ca0 = TFALSE; // dirty flag
					}

					break;
				default: break;
			}
		}
	}

	return TTRUE;
}

TBOOL ARmlOptionsState::ProcessInput( const TInputInterface::InputEvent* a_pInputEvent )
{
	if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN &&
	     a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_ESCAPE )
	{
		s_ePending = PENDING_CLOSE;
	}

	return TTRUE;
}
