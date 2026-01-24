#include "pch.h"
#include "SDLWindow.h"
#include "Toshi/TApplication.h"

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

	return m_pWindow != TNULL;
}

void SDLWindow::Update()
{
	SDL_Event event;

	while ( SDL_PollEvent( &event ) )
	{
		if ( event.type == SDL_EventType::SDL_QUIT )
		{
			exit( 0 );
			//TGlobalEmitter<TApplicationExitEvent>::Throw( { TFALSE } );
		}
	}
}

void SDLWindow::SetFullscreen( TBOOL a_bFullScreen )
{
	SDL_SetWindowFullscreen( m_pWindow, a_bFullScreen ? SDL_WINDOW_FULLSCREEN : 0 );
	m_bFullscreen = a_bFullScreen;
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
