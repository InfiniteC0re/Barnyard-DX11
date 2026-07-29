#include "pch.h"
#include "ARmlFrontEndState.h"
#include "ARmlVideoSettingsState.h"
#include "ARmlOptionsState.h"
#include "ARmlConfirmPopupState.h"
#include "ARmlSaveSlotState.h"
#include "RmlManager.h"
#include "RmlFocusGroup.h"

#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/AGUI2.h>
#include <BYardSDK/SDKHooks.h>

#include <Input/TInputDeviceKeyboard.h>

#include <RmlUi/Core.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_NORUNTIME( ARmlFrontEndState );

// Single active front-end state, so file-static model state is fine
static TBOOL s_bModelReady = TFALSE;

// Selection is restored when returning from a pushed state (options, save slots, ...)
Rml::String s_strLastFocus;

// State changes must run outside the RmlUi click callback (see OnUpdate)
enum EPendingAction
{
	PENDING_NONE,
	PENDING_NEWGAME,
	PENDING_LOADGAME,
	PENDING_OPTIONS,
	PENDING_BONUS,
	PENDING_ANTICS,
	PENDING_QUIT,
	PENDING_CLOSE,
};
EPendingAction s_ePending = PENDING_NONE;

static void OnNewGame()  { s_ePending = PENDING_NEWGAME; }
static void OnLoadGame() { s_ePending = PENDING_LOADGAME; }
static void OnBonus()    { s_ePending = PENDING_BONUS; }
static void OnAntics()   { s_ePending = PENDING_ANTICS; }
static void OnOptions()  { s_ePending = PENDING_OPTIONS; }
static void OnQuit()     { s_ePending = PENDING_QUIT; }

static void PushFrontEndButtonState( TINT a_iButton )
{
	TBYTE aStub[ 0x1400 ] = {};
	*( TINT* )( aStub + 0x13f0 ) = a_iButton;
	CALL_THIS( 0x004099f0, void*, void, aStub );
}

// Confirmed on the quit popup
static void QuitToWindows()
{
	CALL( 0x00425980, void );
	CALL_THIS( 0x006c1760, void*, void, ( void* )0x0077de84 );
}

constexpr TINT LOCALE_QUIT_TITLE   = 333;
constexpr TINT LOCALE_QUIT_MESSAGE = 1065;

ARmlFrontEndState::ARmlFrontEndState( AWindMillHelper* a_pWindMillHelper )
{
	m_pWindMillHelper = a_pWindMillHelper;
}

void ARmlFrontEndState::OnActivate()
{
	// This instance persists on the stack while a pushed state runs, so clear any
	// leftover close state when we return to the menu
	s_ePending   = PENDING_NONE;
	m_bClosing   = TFALSE;
	m_fCloseTime = 0.0f;

	remaster::rml::SetEnabled( TTRUE );
	remaster::rml::LockInput( remaster::rml::APPEAR_ANIMATION_DURATION );

	Rml::Context* pContext = remaster::rml::GetContext();
	if ( !pContext )
		return;

	// Event callbacks bind to the model, which RmlUi keeps registered across
	// RemoveDataModel, so create it once
	if ( !s_bModelReady )
	{
		Rml::DataModelConstructor oModel = pContext->CreateDataModel( "frontend" );

		oModel.BindEventCallback( "newgame", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnNewGame(); } );
		oModel.BindEventCallback( "loadgame", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnLoadGame(); } );
		oModel.BindEventCallback( "options", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnOptions(); } );
		oModel.BindEventCallback( "bonus", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnBonus(); } );
		oModel.BindEventCallback( "antics", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnAntics(); } );
		oModel.BindEventCallback( "quit", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { OnQuit(); } );

		s_bModelReady = true;
	}

	m_pDocument = remaster::rml::LoadDocument( "Data/UI/Layout/frontend.rml" );
	remaster::rml::ShowDocument( m_pDocument );

	// Restore the previous selection, so returning from a submenu lands where the
	// player left off; otherwise start on the first button
	remaster::RestoreFocusedElement( m_pDocument, s_strLastFocus );

	AGUI2::GetSingleton()->SetCursorVisible( TTRUE );
}

void ARmlFrontEndState::OnDeactivate()
{
	AGUI2::GetSingleton()->SetCursorVisible( TFALSE );

	s_strLastFocus = remaster::GetFocusedElementId( m_pDocument );

	remaster::rml::CloseDocument( m_pDocument );
	m_pDocument = TNULL;

	remaster::rml::SetEnabled( TFALSE );
}

TBOOL ARmlFrontEndState::OnUpdate( TFLOAT a_fDeltaTime )
{
	// A queued action first plays the menu's disappear animation, then runs -- and
	// runs here (not in the RmlUi click callback) so the document is not torn down
	// mid-dispatch
	if ( s_ePending != PENDING_NONE && !m_bClosing )
	{
		m_bClosing   = TTRUE;
		m_fCloseTime = 0.0f;
		remaster::rml::BeginCloseAnimation( m_pDocument, "menu" );
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
				case PENDING_OPTIONS:  pController->PushState( new ARmlOptionsState() ); break;
				case PENDING_NEWGAME:  pController->PushState( new ARmlSaveSlotState( ARmlSaveSlotState::MODE_NEW ) ); break;
				case PENDING_LOADGAME: pController->PushState( new ARmlSaveSlotState( ARmlSaveSlotState::MODE_LOAD ) ); break;
				case PENDING_BONUS:    PushFrontEndButtonState( 3 ); break;
				case PENDING_ANTICS:   PushFrontEndButtonState( 4 ); break;
				case PENDING_QUIT:     pController->PushState( new ARmlConfirmPopupState( LOCALE_QUIT_TITLE, LOCALE_QUIT_MESSAGE, QuitToWindows ) ); break;
				case PENDING_CLOSE:    pController->PopCurrentGameState(); break;
				default: break;
			}
		}
	}

	return TTRUE;
}

TBOOL ARmlFrontEndState::ProcessInput( const TInputInterface::InputEvent* a_pInputEvent )
{
	return TTRUE;
}

void ARmlFrontEndState::OnInsertion()
{
	*(TBOOL*)( 0x007810e8 ) = TTRUE; // ms_bIsInserted

	// Spawn barn sign
	// The original method also has a locale check and spawns image instead of the sign sometimes, but we don't want to support it

	if (!m_pWindMillHelper)
	{
		AWindMillHelper* pWindMillHelper = CALL( 0x006b5540, AWindMillHelper*, TINT, 152, void*, TNULL, const TCHAR*, 0, TINT, 0 ); // TMalloc
		CALL_THIS( 0x0054d320, AWindMillHelper*, void, pWindMillHelper );                                                           // AWindMillHelper::AWindMillHelper

		m_pWindMillHelper = pWindMillHelper;

		void* pSimAnimModelHelperManager = *(void**)0x00783e30;
		CALL_THIS( 0x0054b260, void*, void, pSimAnimModelHelperManager, AWindMillHelper*, pWindMillHelper ); // ASimAnimModelHelperManager::AddModelHelper
		CALL_THIS( 0x0054d7c0, AWindMillHelper*, void, pWindMillHelper );                                    // AWindmillHelper::CreateBarnSign
	}

	void* pMusicManager = *(void**)0x0078c42c;
	CALL_THIS( 0x005d46b0, void*, void, pMusicManager, TINT, 26 ); // AMusicManager::PlayBackgroundMusic

	BaseClass::OnInsertion();
}

void ARmlFrontEndState::OnRemoval()
{
	*(TBOOL*)( 0x007810e8 ) = TFALSE; // ms_bIsInserted

	void* pMusicManager = *(void**)0x0078c42c;
	CALL_THIS( 0x005d46d0, void*, void, pMusicManager ); // AMusicManager::StopBackgroundMusic

	void* pSimAnimModelHelperManager = *(void**)0x00783e30;
	CALL_THIS( 0x0054b390, void*, void, pSimAnimModelHelperManager, AWindMillHelper*, m_pWindMillHelper ); // ASimAnimModelHelperManager::RemoveModelHelper

	m_pWindMillHelper->OnDestroy();
	delete m_pWindMillHelper;

	BaseClass::OnRemoval();
}
