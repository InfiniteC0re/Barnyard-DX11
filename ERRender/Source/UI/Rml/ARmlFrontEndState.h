#pragma once
#include <Input/TInputInterface.h>

#include <BYardSDK/AGameState.h>
#include <BYardSDK/AWindMillHelper.h>

namespace Rml
{
class ElementDocument;
}

class ARmlFrontEndState : public AGameState
{
public:
	TDECLARE_CLASS( ARmlFrontEndState, AGameState );

	ARmlFrontEndState( AWindMillHelper* a_pWindMillHelper );

	virtual void  OnActivate() OVERRIDE;
	virtual void  OnDeactivate() OVERRIDE;
	virtual TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual TBOOL ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent ) OVERRIDE;
	virtual void  OnInsertion() OVERRIDE;
	virtual void  OnRemoval() OVERRIDE;

private:
	AWindMillHelper* m_pWindMillHelper = NULL;

	Rml::ElementDocument* m_pDocument = TNULL;

	TBOOL  m_bClosing   = TFALSE;
	TFLOAT m_fCloseTime = 0.0f;
};

// Registers the F6 hotkey that pushes/pops the main menu
void SetupRmlFrontEndState();
