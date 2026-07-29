#pragma once
#include <BYardSDK/AGameState.h>
#include <Input/TInputInterface.h>

namespace Rml
{
class ElementDocument;
}

// Template for a game state driven by RmlUi: enables RmlUi and shows a document
// on entry, closes it and disables RmlUi on exit, and is modal while active
class ARmlDemoState : public AGameState
{
public:
	TDECLARE_CLASS( ARmlDemoState, AGameState );

	ARmlDemoState();

	virtual void  OnActivate() OVERRIDE;
	virtual void  OnDeactivate() OVERRIDE;
	virtual TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual TBOOL ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent ) OVERRIDE;

private:
	Rml::ElementDocument* m_pDocument;
	TINT                  m_iClicks;
};

// Registers the F7 debug hotkey that pushes/pops this state
void SetupRmlDemoState();
