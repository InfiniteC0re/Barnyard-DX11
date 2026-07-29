#include "pch.h"
#include "ARmlConfirmPopupState.h"
#include "RmlManager.h"
#include "RmlFocusGroup.h"

#include <BYardSDK/ASoundManager.h>
#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/AGUI2.h>

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

TDEFINE_CLASS_NORUNTIME( ARmlConfirmPopupState );

namespace
{

// Only one popup can be open at a time, so file-static model state is fine
Rml::String          s_strTitle;
Rml::String          s_strMessage;
Rml::String          s_strYes;
Rml::String          s_strNo;
Rml::DataModelHandle s_hModel;
bool                 s_bModelReady = false;

TBOOL s_bPendingConfirm = TFALSE;
TBOOL s_bPendingCancel  = TFALSE;

constexpr TINT LOCALE_YES = 1097;
constexpr TINT LOCALE_NO  = 1096;

// Bound text goes through the system interface's TranslateString, so a [[N]] marker
// resolves to the localised string -- and arrives as text, never parsed as markup
static Rml::String LocaleMarker( TINT a_iStringId )
{
	TCHAR szMarker[ 16 ];
	T2String8::Format( szMarker, sizeof( szMarker ), "[[%d]]", a_iStringId );
	return Rml::String( szMarker );
}

} // namespace

ARmlConfirmPopupState::ARmlConfirmPopupState( TINT a_iTitleId, TINT a_iMessageId, ConfirmCallback a_pfnOnConfirm )
{
	m_iTitleId     = a_iTitleId;
	m_iMessageId   = a_iMessageId;
	m_pfnOnConfirm = a_pfnOnConfirm;
	m_pDocument    = TNULL;
}

void ARmlConfirmPopupState::OnActivate()
{
	s_bPendingConfirm = TFALSE;
	s_bPendingCancel  = TFALSE;
	m_bClosing        = TFALSE;
	m_fCloseTime      = 0.0f;

	s_strTitle   = LocaleMarker( m_iTitleId );
	s_strMessage = LocaleMarker( m_iMessageId );
	s_strYes     = LocaleMarker( LOCALE_YES );
	s_strNo      = LocaleMarker( LOCALE_NO );

	remaster::rml::SetEnabled( TTRUE );
	remaster::rml::LockInput( remaster::rml::APPEAR_ANIMATION_DURATION );

	Rml::Context* pContext = remaster::rml::GetContext();
	if ( !pContext )
		return;

	if ( !s_bModelReady )
	{
		Rml::DataModelConstructor oModel = pContext->CreateDataModel( "confirm" );

		oModel.Bind( "title", &s_strTitle );
		oModel.Bind( "message", &s_strMessage );
		oModel.Bind( "yesLabel", &s_strYes );
		oModel.Bind( "noLabel", &s_strNo );

		oModel.BindEventCallback( "yes", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { s_bPendingConfirm = TTRUE; } );
		oModel.BindEventCallback( "no", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { s_bPendingCancel = TTRUE; } );

		s_hModel      = oModel.GetModelHandle();
		s_bModelReady = true;
	}
	else
	{
		s_hModel.DirtyAllVariables();
	}

	m_pDocument = remaster::rml::LoadDocument( "Data/UI/Layout/confirm.rml" );
	remaster::rml::ShowDocument( m_pDocument );

	// Default to the safe choice; mute the cue for this programmatic focus
	if ( m_pDocument )
	{
		if ( Rml::Element* pNo = m_pDocument->GetElementById( "btn-no" ) )
		{
			remaster::RmlFocusGroup::SuppressSounds( TTRUE );
			pNo->Focus();
			remaster::RmlFocusGroup::SuppressSounds( TFALSE );
		}
	}

	AGUI2::GetSingleton()->SetCursorVisible( TTRUE );
}

void ARmlConfirmPopupState::OnDeactivate()
{
	AGUI2::GetSingleton()->SetCursorVisible( TFALSE );

	remaster::rml::CloseDocument( m_pDocument );
	m_pDocument = TNULL;

	remaster::rml::SetEnabled( TFALSE );
}

TBOOL ARmlConfirmPopupState::OnUpdate( TFLOAT a_fDeltaTime )
{
	// Play the disappear animation, then act once it finishes (outside the RmlUi
	// callback that queued the choice)
	if ( ( s_bPendingConfirm || s_bPendingCancel ) && !m_bClosing )
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

			const TBOOL bConfirmed = s_bPendingConfirm;
			s_bPendingConfirm = TFALSE;
			s_bPendingCancel  = TFALSE;

			if ( bConfirmed && m_pfnOnConfirm )
				m_pfnOnConfirm();
			else
				AGameStateController::GetSingleton()->PopCurrentGameState();
		}
	}

	return TTRUE;
}

TBOOL ARmlConfirmPopupState::ProcessInput( const TInputInterface::InputEvent* a_pInputEvent )
{
	if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN &&
	     a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_ESCAPE )
	{
		s_bPendingCancel = TTRUE;
	}

	return TTRUE;
}
