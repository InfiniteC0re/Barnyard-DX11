#pragma once
#include <BYardSDK/AGameState.h>
#include <Input/TInputInterface.h>

namespace Rml
{
class ElementDocument;
}

class ARmlVideoSettingsState : public AGameState
{
public:
	TDECLARE_CLASS( ARmlVideoSettingsState, AGameState );

	ARmlVideoSettingsState();

	virtual void  OnActivate() OVERRIDE;
	virtual void  OnDeactivate() OVERRIDE;
	virtual TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual TBOOL ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent ) OVERRIDE;

private:
	Rml::ElementDocument* m_pDocument;
	TBOOL                 m_bFocused   = TFALSE;
	TBOOL                 m_bClosing   = TFALSE;
	TFLOAT                m_fCloseTime = 0.0f;
};

// Registers the F7 hotkey that pushes/pops the settings menu
void SetupRmlSettingsState();
