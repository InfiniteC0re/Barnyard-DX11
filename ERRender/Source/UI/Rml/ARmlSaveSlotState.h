#pragma once
#include <BYardSDK/AGameState.h>
#include <Input/TInputInterface.h>

namespace Rml
{
class ElementDocument;
}

// RmlUi reimplementation of the retail New Game / Load Game slot-select screens
// (AFrontEndNewGameSelectState / AFrontEndLoadState). Reads the 4 save slots from
// APlayerProgress and, on selection, drives the retail new-game / load flow
class ARmlSaveSlotState : public AGameState
{
public:
	TDECLARE_CLASS( ARmlSaveSlotState, AGameState );

	enum EMode
	{
		MODE_NEW,
		MODE_LOAD,
	};

	ARmlSaveSlotState( EMode a_eMode );

	virtual void  OnActivate() OVERRIDE;
	virtual void  OnDeactivate() OVERRIDE;
	virtual TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual TBOOL ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent ) OVERRIDE;

private:
	EMode                 m_eMode;
	Rml::ElementDocument* m_pDocument;
	TBOOL                 m_bFocused   = TFALSE;
	TBOOL                 m_bActed     = TFALSE;
	TBOOL                 m_bClosing   = TFALSE;
	TFLOAT                m_fCloseTime = 0.0f;
};
