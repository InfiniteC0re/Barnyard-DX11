#pragma once
#include <BYardSDK/AGameState.h>
#include <Input/TInputInterface.h>

namespace Rml
{
class ElementDocument;
}

// Options sub-menu (Video / Audio / Controls) backed by an RmlUi document
// (Data/UI/Layout/options.rml). RmlUi reimplementation of the retail AOptionsState.
class ARmlOptionsState : public AGameState
{
public:
	TDECLARE_CLASS( ARmlOptionsState, AGameState );

	ARmlOptionsState();

	virtual void  OnActivate() OVERRIDE;
	virtual void  OnDeactivate() OVERRIDE;
	virtual TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual TBOOL ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent ) OVERRIDE;

private:
	Rml::ElementDocument* m_pDocument;
	TBOOL                 m_bClosing   = TFALSE;
	TFLOAT                m_fCloseTime = 0.0f;
};
