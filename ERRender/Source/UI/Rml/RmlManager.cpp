#include "pch.h"
#include "RmlManager.h"
#include "RmlRenderInterfaceDX11.h"
#include "RmlSystemInterface.h"
#include "RmlSdfFontEngine.h"
#include "RmlFocusGroup.h"
#include "RmlSvgElement.h"
#include "UI/UIRenderer.h"
#include "UI/FontAtlas.h"

#include "ARmlOptionsState.h"
#include "ARmlFrontEndState.h"

#include <BYardSDK/AGUI2.h>

// windowsx.h (pulled in via pch) defines these as window-tree macros that
// collide with Rml::Element member names
#undef GetFirstChild
#undef GetNextSibling
#undef GetPrevSibling
#undef GetNextWindow

#include <RmlUi/Core.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Debugger.h>

#include <AHooks.h>
#include <HookHelpers.h>

#include <BYardSDK/AGameStateController.h>
#include <Input/TInputInterface.h>
#include <Input/TInputDeviceMouse.h>
#include <Input/TInputDeviceKeyboard.h>

#include <GUI/T2GUIContext.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster::rml
{

static RmlRenderInterfaceDX11 s_oRenderInterface;
static RmlSystemInterface     s_oSystemInterface;
static RmlSdfFontEngine       s_oFontEngine;
static Rml::Context*          s_pContext      = TNULL;
static TBOOL                  s_bInitialised  = TFALSE;
static TBOOL                  s_bInitFailed   = TFALSE;
static TINT                   s_iSurfaceWidth  = 0;
static TINT                   s_iSurfaceHeight = 0;
static TFLOAT                 s_flDpRatio      = 0.0f;
static Rml::String            s_strFontFamily  = "Bahnschrift";
static TBOOL                  s_bDebuggerVisible = TFALSE;
static TBOOL                  s_bEnabled       = TFALSE;
static TUINT                  s_uiCapturedButtons = 0;
static void                 ( *s_pfnDebugToggle )() = TNULL;

static Rml::Input::KeyIdentifier TranslateGameKey( TINT a_iDoodad )
{
	switch ( a_iDoodad )
	{
		case TInputDeviceKeyboard::KEY_RETURN: return Rml::Input::KI_RETURN;
		case TInputDeviceKeyboard::KEY_BACK:   return Rml::Input::KI_BACK;
		case TInputDeviceKeyboard::KEY_DELETE: return Rml::Input::KI_DELETE;
		case TInputDeviceKeyboard::KEY_ESCAPE: return Rml::Input::KI_ESCAPE;
		case TInputDeviceKeyboard::KEY_TAB:    return Rml::Input::KI_TAB;
		case TInputDeviceKeyboard::KEY_LEFT:   return Rml::Input::KI_LEFT;
		case TInputDeviceKeyboard::KEY_RIGHT:  return Rml::Input::KI_RIGHT;
		case TInputDeviceKeyboard::KEY_UP:     return Rml::Input::KI_UP;
		case TInputDeviceKeyboard::KEY_DOWN:   return Rml::Input::KI_DOWN;
		case TInputDeviceKeyboard::KEY_HOME:   return Rml::Input::KI_HOME;
		case TInputDeviceKeyboard::KEY_END:    return Rml::Input::KI_END;
		default:                               return Rml::Input::KI_UNKNOWN;
	}
}

static TBOOL Initialise()
{
	if ( !g_pRender || !g_pUIRender )
		return TFALSE;

	const TINT iWidth  = TINT( g_pRender->GetSurfaceWidth() );
	const TINT iHeight = TINT( g_pRender->GetSurfaceHeight() );
	if ( iWidth <= 0 || iHeight <= 0 )
		return TFALSE;

	if ( !s_oRenderInterface.Create() )
		return TFALSE;

	Rml::SetSystemInterface( &s_oSystemInterface );
	Rml::SetRenderInterface( &s_oRenderInterface );
	Rml::SetFontEngineInterface( &s_oFontEngine );

	if ( !Rml::Initialise() )
		return TFALSE;

	RegisterFocusGroup();
	RegisterSvgElement();

	// Text is rendered by the game's SDF atlases, so it stays sharp at any scale
	Rml::LoadFontFace( "Rekord26" );
	Rml::LoadFontFace( "Rekord18" );
	s_strFontFamily = "Rekord26";

	s_oRenderInterface.RegisterFontTexture( g_pRender->GetFontAtlas( RenderDX11::FONT_REKORD26 )->GetTextureResource() );
	s_oRenderInterface.RegisterFontTexture( g_pRender->GetFontAtlas( RenderDX11::FONT_REKORD18 )->GetTextureResource() );

	s_pContext = Rml::CreateContext( "main", Rml::Vector2i( iWidth, iHeight ) );
	if ( !s_pContext )
	{
		Rml::Shutdown();
		return TFALSE;
	}

	Rml::Debugger::Initialise( s_pContext );

	TUtil::Log( "[RmlUi] Initialised (%dx%d)\n", iWidth, iHeight );
	return TTRUE;
}

// Idempotent init that does not depend on the enabled flag, so documents can be
// loaded before the first render
static TBOOL EnsureInitialised()
{
	if ( s_bInitialised )
		return TTRUE;

	if ( s_bInitFailed )
		return TFALSE;

	if ( !g_pRender || !g_pUIRender || g_pRender->GetSurfaceWidth() <= 0.0f )
		return TFALSE;

	if ( !Initialise() )
	{
		s_bInitFailed = TTRUE;
		TUtil::Log( "[RmlUi] Initialisation failed\n" );
		return TFALSE;
	}

	s_bInitialised = TTRUE;
	return TTRUE;
}

// Drive the pointer from AGUI2's virtual cursor. The game grabs the OS mouse
// for the camera, so both position and buttons come from the game's input, not
// the OS: position here, buttons/keys via the ProcessInput hook below.
static void UpdateInput()
{
	AGUI2MouseCursor& rCursor = AGUI2::GetSingleton()->m_oMouseCursor;

	TFLOAT fCanvasW, fCanvasH;
	AGUI2::GetRootElement()->GetDimensions( fCanvasW, fCanvasH );

	// m_MousePos is centred on the canvas; the context is in native pixels
	const TFLOAT fX = ( rCursor.m_MousePos.x + fCanvasW * 0.5f ) / fCanvasW * s_iSurfaceWidth;
	const TFLOAT fY = ( rCursor.m_MousePos.y + fCanvasH * 0.5f ) / fCanvasH * s_iSurfaceHeight;

	s_pContext->ProcessMouseMove( TINT( fX ), TINT( fY ), 0 );
}

void RenderFrame()
{
	if ( !s_bEnabled )
		return;

	if ( !EnsureInitialised() )
		return;

	// Context runs at native resolution so glyphs stay sharp
	const TINT iWidth  = TINT( g_pRender->GetSurfaceWidth() );
	const TINT iHeight = TINT( g_pRender->GetSurfaceHeight() );
	if ( iWidth != s_iSurfaceWidth || iHeight != s_iSurfaceHeight )
	{
		s_iSurfaceWidth  = iWidth;
		s_iSurfaceHeight = iHeight;
		s_pContext->SetDimensions( Rml::Vector2i( iWidth, iHeight ) );
	}

	// Scale documents like the AGUI2 canvas via the dp ratio: dp units track the
	// canvas height (936x702 widescreen, 800x600 otherwise) but render at native res
	TFLOAT fCanvasW, fCanvasH;
	AGUI2::GetRootElement()->GetDimensions( fCanvasW, fCanvasH );
	const TFLOAT fDpRatio = ( fCanvasH > 0.0f ) ? ( iHeight / fCanvasH ) : 1.0f;
	if ( fDpRatio != s_flDpRatio )
	{
		s_flDpRatio = fDpRatio;
		s_pContext->SetDensityIndependentPixelRatio( fDpRatio );
		s_oFontEngine.SetDpRatio( fDpRatio );
	}

	// Freeze the pointer (no hover/focus changes) while an animation is playing
	if ( !IsInputLocked() )
		UpdateInput();
	s_pContext->Update();

	s_oRenderInterface.BeginFrame();
	s_pContext->Render();
	s_oRenderInterface.EndFrame();
}

// Re-read every open document's .rcss from disk (drop the cache first so the
// files are actually re-parsed). Live-iterate styling without restarting
static void ReloadStyleSheets()
{
	if ( !s_bInitialised || !s_pContext )
		return;

	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();

	for ( int i = 0; i < s_pContext->GetNumDocuments(); i++ )
		s_pContext->GetDocument( i )->ReloadStyleSheet();

	TUtil::Log( "[RmlUi] Reloaded stylesheets\n" );
}

void Shutdown()
{
	if ( !s_bInitialised )
		return;

	Rml::Shutdown();
	s_oRenderInterface.Destroy();
	s_pContext     = TNULL;
	s_bInitialised = TFALSE;
}

// Returns TTRUE when RmlUi consumed the event, so the caller can withhold it
// from the game (e.g. a click that landed on a document)
static TBOOL OnInputEvent( const TInputInterface::InputEvent* a_pEvent )
{
	TInputDevice*   pSource = a_pEvent->GetSource();
	const TINT      iDoodad = a_pEvent->GetDoodad();
	const TBOOL     bDown   = a_pEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN;
	const TBOOL     bUp     = a_pEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_UP;

	TClass* pMouseClass    = (TClass*)0x007cec68;
	TClass* pKeyboardClass = (TClass*)0x007cef54;

	// Debug toggles, work whether or not RmlUi is currently enabled
	if ( bDown && pSource->IsA( pKeyboardClass ) && iDoodad == TInputDeviceKeyboard::KEY_F7 )
	{
		if ( s_pfnDebugToggle )
			s_pfnDebugToggle();
		else
			SetEnabled( !s_bEnabled );
		return TFALSE;
	}

	if ( !s_bEnabled || !s_bInitialised )
		return TFALSE;

	// Swallow clicks/keys while an appear or disappear animation is playing
	if ( IsInputLocked() )
		return TTRUE;

	if ( pSource->IsA( pMouseClass ) )
	{
		if ( bDown && iDoodad == TInputDeviceMouse::BUTTON_WHEEL_FORWARD )
			return !s_pContext->ProcessMouseWheel( -1.0f, 0 );
		if ( bDown && iDoodad == TInputDeviceMouse::BUTTON_WHEEL_BACKWARD )
			return !s_pContext->ProcessMouseWheel( 1.0f, 0 );

		TINT iButton = -1;
		switch ( iDoodad )
		{
			case TInputDeviceMouse::BUTTON_1: iButton = 0; break;
			case TInputDeviceMouse::BUTTON_2: iButton = 1; break;
			case TInputDeviceMouse::BUTTON_3: iButton = 2; break;
		}
		if ( iButton < 0 )
			return TFALSE;

		const TUINT uiMask = 1u << iButton;

		if ( bDown )
		{
			const TBOOL bConsumed = !s_pContext->ProcessMouseButtonDown( iButton, 0 );
			if ( bConsumed )
				s_uiCapturedButtons |= uiMask;
			return bConsumed;
		}

		if ( bUp )
		{
			const TBOOL bConsumed  = !s_pContext->ProcessMouseButtonUp( iButton, 0 );
			// Always swallow the release of a press we captured, so the game
			// does not see an unpaired button-up and get stuck
			const TBOOL bWasCaptured = ( s_uiCapturedButtons & uiMask ) != 0;
			s_uiCapturedButtons &= ~uiMask;
			return bConsumed || bWasCaptured;
		}

		return TFALSE;
	}

	if ( pSource->IsA( pKeyboardClass ) )
	{
		if ( iDoodad == TInputDeviceKeyboard::KEY_F8 )
		{
			if ( bDown )
			{
				s_bDebuggerVisible = !s_bDebuggerVisible;
				Rml::Debugger::SetVisible( s_bDebuggerVisible );
			}
			return TFALSE;
		}

		if ( iDoodad == TInputDeviceKeyboard::KEY_F9 )
		{
			if ( bDown )
				ReloadStyleSheets();
			return TFALSE;
		}

		const Rml::Input::KeyIdentifier eKey = TranslateGameKey( iDoodad );

		TBOOL bConsumed = TFALSE;
		if ( bDown )
		{
			if ( eKey != Rml::Input::KI_UNKNOWN )
				bConsumed = !s_pContext->ProcessKeyDown( eKey, 0 );

			const TWCHAR* pwChar = TSTATICCAST( TInputDeviceKeyboard, pSource )->TranslateDoodadToCharacter( iDoodad );
			if ( pwChar && pwChar[ 0 ] >= 0x20 )
				s_pContext->ProcessTextInput( Rml::Character( pwChar[ 0 ] ) );
		}
		else if ( bUp && eKey != Rml::Input::KI_UNKNOWN )
		{
			bConsumed = !s_pContext->ProcessKeyUp( eKey, 0 );
		}
		return bConsumed;
	}

	return TFALSE;
}

void OnResize( TINT a_iWidth, TINT a_iHeight )
{
	if ( s_bInitialised && s_pContext )
		s_pContext->SetDimensions( Rml::Vector2i( a_iWidth, a_iHeight ) );
}

Rml::ElementDocument* LoadDocument( const TCHAR* a_szPath )
{
	if ( !EnsureInitialised() )
		return TNULL;

	return s_pContext->LoadDocument( a_szPath );
}

Rml::ElementDocument* LoadDocumentFromMemory( const TCHAR* a_szRml )
{
	if ( !EnsureInitialised() )
		return TNULL;

	return s_pContext->LoadDocumentFromMemory( a_szRml );
}

void ShowDocument( Rml::ElementDocument* a_pDocument )
{
	if ( a_pDocument )
		a_pDocument->Show();
}

void HideDocument( Rml::ElementDocument* a_pDocument )
{
	if ( a_pDocument )
		a_pDocument->Hide();
}

void CloseDocument( Rml::ElementDocument* a_pDocument )
{
	if ( a_pDocument )
		a_pDocument->Close();
}

const TCHAR* GetFontFamily()
{
	EnsureInitialised();
	return s_strFontFamily.c_str();
}

static double s_dInputLockedUntil = 0.0;

void LockInput( TFLOAT a_flSeconds )
{
	const double dEnd = s_oSystemInterface.GetElapsedTime() + a_flSeconds;
	if ( dEnd > s_dInputLockedUntil )
		s_dInputLockedUntil = dEnd;
}

TBOOL IsInputLocked()
{
	return s_oSystemInterface.GetElapsedTime() < s_dInputLockedUntil;
}

void BeginCloseAnimation( Rml::ElementDocument* a_pDocument, const TCHAR* a_szElementId )
{
	LockInput( CLOSE_ANIMATION_DURATION );

	if ( !a_pDocument )
		return;

	if ( Rml::Element* pElement = a_pDocument->GetElementById( a_szElementId ) )
		pElement->SetClass( "closing", true );
}

void SetDebugToggleHandler( void ( *a_pfnHandler )() )
{
	s_pfnDebugToggle = a_pfnHandler;
}

void SetEnabled( TBOOL a_bEnabled )
{
	if ( s_bEnabled == a_bEnabled )
		return;

	s_bEnabled = a_bEnabled;

	// Drop hover/press state so the UI does not resume mid-interaction
	if ( !s_bEnabled )
	{
		s_uiCapturedButtons = 0;
		if ( s_bInitialised )
			s_pContext->ProcessMouseLeave();
	}
}

TBOOL IsEnabled()
{
	return s_bEnabled;
}

TBOOL IsInitialised()
{
	return s_bInitialised;
}

Rml::Context* GetContext()
{
	EnsureInitialised();
	return s_pContext;
}

} // namespace remaster::rml

MEMBER_HOOK( 0x004293d0, AGameStateController, AGameStateController_ProcessInput, TBOOL, TInputInterface::InputEvent* a_pInputEvent )
{
	if ( remaster::rml::OnInputEvent( a_pInputEvent ) )
		return TTRUE;

	return CallOriginal( a_pInputEvent );
}

class AOptionsState {};

MEMBER_HOOK( 0x00444ef0, AOptionsState, AOptionsState_Constructor, ARmlOptionsState* )
{
	TSTATICASSERT( sizeof( ARmlOptionsState ) <= 3044 );
	return new ( this ) ARmlOptionsState( );
}

class AFrontEndMainMenuState2 {};

MEMBER_HOOK( 0x00408f20, AFrontEndMainMenuState2, AFrontEndMainMenuState2_Constructor, ARmlFrontEndState*, AWindMillHelper* a_pWindMillHelper, TBOOL a_bFlag )
{
	TSTATICASSERT( sizeof( ARmlFrontEndState ) <= 5400 );
	return new ( this ) ARmlFrontEndState( a_pWindMillHelper );
}

MEMBER_HOOK( 0x006c4820, T2GUIContext, T2GUIContext_Render, void )
{
	remaster::rml::RenderFrame();

	CallOriginal();
}

void remaster::rml::SetupHooks()
{
	InstallHook<AFrontEndMainMenuState2_Constructor>();
	InstallHook<AOptionsState_Constructor>();
	InstallHook<AGameStateController_ProcessInput>();
	InstallHook<T2GUIContext_Render>();
}
