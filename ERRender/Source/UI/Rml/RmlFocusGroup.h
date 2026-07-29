#pragma once
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/EventListener.h>

namespace Rml
{
class ElementDocument;
}

namespace remaster
{

// Custom <focusgroup> element: focuses whichever focusable descendant the mouse
// hovers, so the pointer shares the keyboard/controller focus highlight. Keyboard
// navigation itself (Tab, arrow nav, Enter) is left to RmlUi's built-in document
// navigation, driven by the tab-index and nav properties. Register once with
// RegisterFocusGroup() before any document is loaded
class RmlFocusGroup : public Rml::Element, public Rml::EventListener
{
public:
	RmlFocusGroup( const Rml::String& a_rTag );
	~RmlFocusGroup() OVERRIDE;

	void ProcessEvent( Rml::Event& a_rEvent ) OVERRIDE;

	// Mutes the focus cue around a programmatic Focus() call (e.g. the default
	// selection set when a menu opens) so it does not sound on open
	static void SuppressSounds( TBOOL a_bSuppress );

private:
	// Play the cue named by an attribute (element's own wins, else the group's)
	void PlayCue( Rml::Element* a_pElement, const Rml::String& a_rAttribute );

	// Sound a selection change once, skipping repeats of the same element so
	// moving within a row (or re-hovering the current one) stays silent
	void SoundSelection( Rml::Element* a_pElement );

	// Move focus to the nearest focusable descendant in the pressed arrow direction,
	// crossing scroll containers (RmlUi's built-in nav is scoped to a single one).
	// Opt-in per group via the nav-spatial attribute
	void SpatialNavigate( Rml::Element* a_pCurrent, TINT a_iKey );

	Rml::Element* m_pLastSounded = TNULL;
};

void RegisterFocusGroup();

// Id of the element currently holding focus in the document, or an empty string if the
// focus is nowhere or on an element without an id. Menus record this before a pushed
// state closes their document, so the selection survives the round trip
Rml::String GetFocusedElementId( Rml::ElementDocument* a_pDocument );

// Focuses the first focusable element inside a_pRoot, skipping hidden ones -- so a
// tabset's panels land on the control at the top of the active tab. Returns whether
// anything was focused. Muted, like the other programmatic focus helpers
TBOOL FocusFirstFocusable( Rml::Element* a_pRoot );

// Focuses a_rId if it still exists, else a_szFallbackId, else the first focusable
// element in the document (pass TNULL to always start at the top of the menu). The focus
// cue is muted, since this is a programmatic selection rather than the user moving
void RestoreFocusedElement( Rml::ElementDocument* a_pDocument, const Rml::String& a_rId, const TCHAR* a_szFallbackId = TNULL );

} // namespace remaster
