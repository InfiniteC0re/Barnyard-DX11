#include "pch.h"
#include "SDLWindow.h"
#include "Toshi/TApplication.h"
#include "Toshi/TSystem.h"

#include <SDL/SDL_syswm.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( remaster::SDLWindow );

namespace remaster
{

TBOOL SDLWindow::Create( RenderDX11* a_pRender, const TCHAR* a_szTitle )
{
	// Set app name to make keyboard events work
	HINSTANCE hInst = GetModuleHandle( NULL );
	if ( SDL_RegisterApp( "TRenderD3DInterface", 0, hInst ) == -1 )
	{
		TERROR( "SDL_RegisterApp failed: %s\n", SDL_GetError() );
		return TFALSE;
	}

	if ( TINT iResult = SDL_Init( SDL_INIT_VIDEO ); iResult != 0 )
	{
		TERROR( "SDL_Init failed: %s\n", SDL_GetError() );
		return TFALSE;
	}

	m_pRender = a_pRender;
	m_pWindow = SDL_CreateWindow(
	    a_szTitle,
	    SDL_WINDOWPOS_CENTERED,
	    SDL_WINDOWPOS_CENTERED,
	    800,
	    600,
	    SDL_WINDOW_HIDDEN
	);

	SDL_HideWindow( m_pWindow );
	SDL_SetRelativeMouseMode( SDL_TRUE );

	SDL_SysWMinfo wmInfo;
	SDL_VERSION( &wmInfo.version );
	SDL_GetWindowWMInfo( GetSDLHandle(), &wmInfo );
	m_hHandle = wmInfo.info.win.window;

	return m_pWindow != TNULL;
}

void SDLWindow::Update()
{
	SDL_Event event;

	// NOTE: required to replicate some bugs happening on alt+tabs
	// We must replicate original behavior here, even though it's a mess
	TUINT32 uiFlags  = SDL_GetWindowFlags( m_pWindow );
	TBOOL   bFocused = uiFlags & SDL_WINDOW_INPUT_FOCUS;
	do 
	{
		while ( SDL_PollEvent( &event ) )
		{
			if ( event.type == SDL_EventType::SDL_QUIT )
			{
				TerminateProcess( GetCurrentProcess(), 0 );
				//TGlobalEmitter<TApplicationExitEvent>::Throw( { TFALSE } );
			}
			else if ( event.type == SDL_EventType::SDL_WINDOWEVENT )
			{
				Toshi::TSystemManager* pGameSM = (Toshi::TSystemManager*)0x007ce640;

				if ( event.window.event == SDL_WINDOWEVENT_FOCUS_LOST || event.window.event == SDL_WINDOWEVENT_HIDDEN )
				{
					SDL_SetRelativeMouseMode( SDL_FALSE );
					bFocused = TFALSE;
				}
				else if ( event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || event.window.event == SDL_WINDOWEVENT_SHOWN )
				{
					SDL_SetRelativeMouseMode( SDL_TRUE );
					bFocused = TTRUE;
				}
			}
		}
	} while ( !bFocused );
}

void SDLWindow::SetFullscreen( TBOOL a_bFullscreen, TBOOL a_bBorderless )
{
	TUINT32 uiFlags = 0;
	if ( a_bFullscreen )
		uiFlags = a_bBorderless ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;

	SDL_SetWindowFullscreen( m_pWindow, uiFlags );
}

void SDLWindow::SetExclusiveDisplayMode( TINT a_iWidth, TINT a_iHeight )
{
	SDL_DisplayMode oWant = {};
	oWant.w               = a_iWidth;
	oWant.h               = a_iHeight;

	SDL_DisplayMode oClosest;
	const TINT      iDisplayIndex = SDL_GetWindowDisplayIndex( m_pWindow );
	if ( SDL_GetClosestDisplayMode( iDisplayIndex, &oWant, &oClosest ) )
		SDL_SetWindowDisplayMode( m_pWindow, &oClosest );
}

void SDLWindow::SetPosition( TINT a_iX, TINT a_iY, TINT a_iWidth, TINT a_iHeight )
{
	SDL_SetWindowSize( m_pWindow, a_iWidth, a_iHeight );
	SDL_SetWindowPosition( m_pWindow, a_iX, a_iY );
}

void SDLWindow::Show()
{
	SDL_ShowWindow( m_pWindow );
	SDL_RaiseWindow( m_pWindow );
}

} // namespace remaster
