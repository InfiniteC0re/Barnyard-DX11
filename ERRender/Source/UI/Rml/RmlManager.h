#pragma once

namespace Rml
{
class Context;
class ElementDocument;
} // namespace Rml

namespace remaster::rml
{

// Lazy-inits on first call once the render device and UI shaders are ready
void RenderFrame();
void Shutdown();

// Installs the input (ProcessInput) and render (AGUI2 post-render) hooks
void SetupHooks();

void OnResize( TINT a_iWidth, TINT a_iHeight );

// Global on/off. Off by default: RmlUi neither renders nor captures input.
// Game states that need RmlUi flip this on entry/exit.
void  SetEnabled( TBOOL a_bEnabled );
TBOOL IsEnabled();

// Documents. Callers hold the returned pointer as an opaque handle; the
// Show/Hide/Close wrappers keep them from having to include RmlUi headers.
Rml::ElementDocument* LoadDocument( const TCHAR* a_szPath );
Rml::ElementDocument* LoadDocumentFromMemory( const TCHAR* a_szRml );
void                  ShowDocument( Rml::ElementDocument* a_pDocument );
void                  HideDocument( Rml::ElementDocument* a_pDocument );
void                  CloseDocument( Rml::ElementDocument* a_pDocument );

// Family name of the loaded font, for building documents
const TCHAR* GetFontFamily();

// Animation timings (must match the CSS on #window / #menu)
constexpr TFLOAT APPEAR_ANIMATION_DURATION = 0.4f;
constexpr TFLOAT CLOSE_ANIMATION_DURATION  = 0.4f;

// Disappear animation: add the "closing" class to the named element so its CSS
// transition plays (and lock input for the duration), then finalise the close
void BeginCloseAnimation( Rml::ElementDocument* a_pDocument, const TCHAR* a_szElementId );

// Swallow all UI input for a_flSeconds, so nothing is interactive while an appear
// or disappear animation is playing. Extends an active lock, never shortens it
void  LockInput( TFLOAT a_flSeconds );
TBOOL IsInputLocked();

// Optional F7 handler (e.g. push/pop a game state); falls back to SetEnabled
void SetDebugToggleHandler( void ( *a_pfnHandler )() );

// Optional F6 handler (push/pop the main menu state)
void SetFrontEndToggleHandler( void ( *a_pfnHandler )() );

TBOOL         IsInitialised();
Rml::Context* GetContext();

} // namespace remaster::rml
