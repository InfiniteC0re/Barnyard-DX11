#pragma once

#include <SDL/SDL.h>

namespace remaster
{

class RenderDX11;

class SDLWindow : public Toshi::TObject
{
public:
	TDECLARE_CLASS( SDLWindow, Toshi::TObject );

public:
	SDLWindow()  = default;
	virtual ~SDLWindow() = default;

	TBOOL Create( RenderDX11* a_pRender, const TCHAR* a_szTitle );
	void  Update();

	void SetFullscreen( TBOOL a_bFullScreen );
	void SetPosition( TINT a_iX, TINT a_iY, TINT a_iWidth, TINT a_iHeight );
	void Show();

	SDL_Window* GetSDLHandle() const { return m_pWindow; }

private:
	SDL_Window*    m_pWindow;
	RenderDX11*    m_pRender;
	TBOOL          m_bFullscreen = TFALSE;
	HMODULE        m_ModuleHandle;
};

// Need to preserve original size of TMSWindow (20 bytes)!!!
TSTATICASSERT( sizeof( SDLWindow ) == 20 );

} // namespace remaster
