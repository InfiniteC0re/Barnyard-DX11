#include "pch.h"
#include "ARmlVideoSettingsState.h"
#include "RmlManager.h"
#include "RmlFocusGroup.h"
#include "GameSettings.h"
#include "RenderDX11.h"

#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/AGUI2.h>
#include <BYardSDK/ARootTask.h>
#include <BYardSDK/ASoundManager.h>

#include <Input/TInputDeviceKeyboard.h>
#include <Toshi/T2String8.h>

#include <RmlUi/Core.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( ARmlVideoSettingsState );

struct EffectItem
{
	Rml::String name;
	bool        enabled;
};

// Bound to the data model; single active settings state so file-static is fine
Rml::Vector<Rml::String> s_vecResolutions;
Rml::Vector<EffectItem>  s_vecEffects;
int                      s_iResIndex    = -1;
int                      s_iDisplayMode = 0;
int                      s_iMSAA        = 0;
int                      s_iPostAA      = 0;
int                      s_iCSM         = 0;
bool                     s_bVSync       = false;
Rml::DataModelHandle     s_hModel;
bool                     s_bModelReady   = false;
bool                     s_bPendingClose = false;

const TUINT  s_aMSAASamples[]  = { 1, 2, 4, 8 };
const TCHAR* s_aEffectLabels[] = {
	"Shadows (CSM)", "Cloud Shadows", "Ambient Occlusion", "Screen-Space Reflections",
	"HDR Bloom", "Sun Shafts", "Volumetric Fog", "Dynamic Lights", "Dynamic Light Shadows"
};

static void PopulateFromCurrent()
{
	using namespace remaster;

	RenderDX11*            pRender = remaster::g_pRender;
	const GraphicsSettings oCur    = pRender->GetGraphicsSettings();

	const Toshi::T2DynamicVector<RenderDX11::Resolution>& rcRes = pRender->GetAvailableResolutions();

	s_vecResolutions.clear();
	s_iResIndex = -1;
	for ( TINT i = 0; i < rcRes.Size(); i++ )
	{
		TCHAR szLabel[ 32 ];
		T2String8::Format( szLabel, sizeof( szLabel ), "%u x %u", rcRes[ i ].uiWidth, rcRes[ i ].uiHeight );
		s_vecResolutions.push_back( szLabel );

		if ( rcRes[ i ].uiWidth == oCur.uiWidth && rcRes[ i ].uiHeight == oCur.uiHeight )
			s_iResIndex = i;
	}

	s_iDisplayMode = TINT( oCur.eDisplayMode );
	s_bVSync       = oCur.bVSync != TFALSE;
	s_iPostAA      = TINT( oCur.eAAMode );
	s_iCSM         = TINT( oCur.eCSMPreset );

	s_iMSAA = 0;
	for ( TINT i = 0; i < TARRAYSIZE( s_aMSAASamples ); i++ )
		if ( s_aMSAASamples[ i ] == oCur.uiMSAASamples )
			s_iMSAA = i;

	s_vecEffects.clear();
	for ( TINT i = 0; i < GameSettings::GetEffectCount(); i++ )
	{
		EffectItem oItem;
		oItem.name    = ( i < TARRAYSIZE( s_aEffectLabels ) ) ? s_aEffectLabels[ i ] : "Effect";
		oItem.enabled = GameSettings::GetEffectEnabled( i ) != TFALSE;
		s_vecEffects.push_back( oItem );
	}
}

static void ApplySettings()
{
	using namespace remaster;

	RenderDX11*                                           pRender = remaster::g_pRender;
	const Toshi::T2DynamicVector<RenderDX11::Resolution>& rcRes   = pRender->GetAvailableResolutions();

	GraphicsSettings oNew = pRender->GetGraphicsSettings();

	if ( s_iResIndex >= 0 && s_iResIndex < rcRes.Size() )
	{
		oNew.uiWidth  = rcRes[ s_iResIndex ].uiWidth;
		oNew.uiHeight = rcRes[ s_iResIndex ].uiHeight;
	}

	oNew.eDisplayMode  = DisplayMode( s_iDisplayMode );
	oNew.bVSync        = s_bVSync ? TTRUE : TFALSE;
	oNew.uiMSAASamples = s_aMSAASamples[ ( s_iMSAA >= 0 && s_iMSAA < TARRAYSIZE( s_aMSAASamples ) ) ? s_iMSAA : 0 ];
	oNew.eAAMode       = AAMode( s_iPostAA );
	oNew.eCSMPreset    = CSMPreset( s_iCSM );

	pRender->RequestDisplayMode( oNew.eDisplayMode );
	pRender->RequestResolution( oNew.uiWidth, oNew.uiHeight );
	pRender->RequestVSync( oNew.bVSync );
	pRender->RequestMSAA( oNew.uiMSAASamples );
	pRender->RequestCSMPreset( oNew.eCSMPreset );
	pRender->RequestAAMode( oNew.eAAMode );

	for ( TINT i = 0; i < TINT( s_vecEffects.size() ) && i < GameSettings::GetEffectCount(); i++ )
		GameSettings::SetEffectEnabled( i, s_vecEffects[ i ].enabled ? TTRUE : TFALSE );

	GameSettings::Save( "Data\\GameSettings.xml", oNew );
}

ARmlVideoSettingsState::ARmlVideoSettingsState()
{
	m_pDocument = TNULL;
}

void ARmlVideoSettingsState::OnActivate()
{
	s_bPendingClose = false;
	m_bFocused      = TFALSE;

	remaster::rml::SetEnabled( TTRUE );
	remaster::rml::LockInput( remaster::rml::APPEAR_ANIMATION_DURATION );

	PopulateFromCurrent();

	Rml::Context* pContext = remaster::rml::GetContext();
	if ( !pContext )
		return;

	// Create the model and register its types once; RmlUi keeps struct/array type
	// registrations alive across RemoveDataModel, so re-registering would throw
	if ( !s_bModelReady )
	{
		Rml::DataModelConstructor oModel = pContext->CreateDataModel( "settings" );

		if ( auto oStruct = oModel.RegisterStruct<EffectItem>() )
		{
			oStruct.RegisterMember( "name", &EffectItem::name );
			oStruct.RegisterMember( "enabled", &EffectItem::enabled );
		}
		oModel.RegisterArray<Rml::Vector<EffectItem>>();
		oModel.RegisterArray<Rml::Vector<Rml::String>>();

		oModel.Bind( "resolutions", &s_vecResolutions );
		oModel.Bind( "resIndex", &s_iResIndex );
		oModel.Bind( "displayMode", &s_iDisplayMode );
		oModel.Bind( "vsync", &s_bVSync );
		oModel.Bind( "msaa", &s_iMSAA );
		oModel.Bind( "postAA", &s_iPostAA );
		oModel.Bind( "csm", &s_iCSM );
		oModel.Bind( "effects", &s_vecEffects );

		oModel.BindEventCallback( "apply", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { ApplySettings(); } );
		oModel.BindEventCallback( "revert", []( Rml::DataModelHandle a_hModel, Rml::Event&, const Rml::VariantList& ) { PopulateFromCurrent(); a_hModel.DirtyAllVariables(); } );
		oModel.BindEventCallback( "close", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) { s_bPendingClose = true; } );

		s_hModel      = oModel.GetModelHandle();
		s_bModelReady = true;
	}
	else
	{
		// Model persists in the context; just refresh the UI from the new values
		s_hModel.DirtyAllVariables();
	}

	m_pDocument = remaster::rml::LoadDocument( "Data/UI/Layout/video_settings.rml" );
	remaster::rml::ShowDocument( m_pDocument );

	AGUI2::GetSingleton()->SetCursorVisible( TTRUE );
}

void ARmlVideoSettingsState::OnDeactivate()
{
	AGUI2::GetSingleton()->SetCursorVisible( TFALSE );

	remaster::rml::CloseDocument( m_pDocument );
	m_pDocument = TNULL;

	// Keep the data model registered so it is not re-created on the next open
	remaster::rml::SetEnabled( TFALSE );
}

TBOOL ARmlVideoSettingsState::OnUpdate( TFLOAT a_fDeltaTime )
{
	// Settings rows are generated on the context's first update, so start the selection
	// on the active tab's first control once they exist (hidden panels are skipped)
	if ( !m_bFocused && m_pDocument )
	{
		Rml::ElementList vecPanels;
		m_pDocument->GetElementsByTagName( vecPanels, "panels" );

		if ( !vecPanels.empty() && remaster::FocusFirstFocusable( vecPanels[ 0 ] ) )
			m_bFocused = TTRUE;
	}

	// Play the disappear animation, then pop once it has finished. Popping happens
	// here (not in the RmlUi callback) so the document is not torn down mid-dispatch
	if ( s_bPendingClose && !m_bClosing )
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
			m_bClosing      = TFALSE;
			s_bPendingClose = false;
			AGameStateController::GetSingleton()->PopCurrentGameState();
		}
	}

	return TTRUE;
}

TBOOL ARmlVideoSettingsState::ProcessInput( const TInputInterface::InputEvent* a_pInputEvent )
{
	if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN &&
	     a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_ESCAPE )
	{
		s_bPendingClose = true;
	}

	return TTRUE;
}
