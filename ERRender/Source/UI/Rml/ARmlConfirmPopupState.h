#pragma once
#include <BYardSDK/AGameState.h>
#include <Input/TInputInterface.h>

namespace Rml
{
class ElementDocument;
}

// Reusable yes/no window (Data/UI/Layout/confirm.rml), reconstructing the retail
// AMessagePopupState dialogs. Texts are locale string ids, resolved through the
// [[N]] markers the system interface already translates.
//
// Confirming runs the callback once the disappear animation has played, and leaves the
// stack alone -- the callback decides what happens next (quit, boot a game, ...).
// Cancelling just pops back to whatever pushed the popup
class ARmlConfirmPopupState : public AGameState
{
public:
	TDECLARE_CLASS( ARmlConfirmPopupState, AGameState );

	typedef void ( *ConfirmCallback )();

	ARmlConfirmPopupState( TINT a_iTitleId, TINT a_iMessageId, ConfirmCallback a_pfnOnConfirm );

	virtual void  OnActivate() OVERRIDE;
	virtual void  OnDeactivate() OVERRIDE;
	virtual TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual TBOOL ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent ) OVERRIDE;

private:
	TINT                  m_iTitleId;
	TINT                  m_iMessageId;
	ConfirmCallback       m_pfnOnConfirm;
	Rml::ElementDocument* m_pDocument;
	TBOOL                 m_bClosing   = TFALSE;
	TFLOAT                m_fCloseTime = 0.0f;
};
