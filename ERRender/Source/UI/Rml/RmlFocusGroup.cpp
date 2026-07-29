#include "pch.h"
#include "RmlFocusGroup.h"

#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/ScrollTypes.h>
#include <RmlUi/Core/ElementDocument.h>

#include "RmlManager.h"

#include <BYardSDK/ASoundManager.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

namespace remaster
{

static TBOOL s_bSuppressSounds = TFALSE;

void RmlFocusGroup::SuppressSounds( TBOOL a_bSuppress )
{
	s_bSuppressSounds = a_bSuppress;
}

// Depth-first list of visible focusable descendants. Focusable controls are not
// descended into, so a select's internals are not collected
static void CollectFocusable( Rml::Element* a_pRoot, Rml::Vector<Rml::Element*>& a_rOut )
{
	const int iNumChildren = a_pRoot->GetNumChildren();
	for ( int i = 0; i < iNumChildren; i++ )
	{
		Rml::Element* pChild = a_pRoot->GetChild( i );

		if ( !pChild->IsVisible() )
			continue;

		if ( pChild->GetComputedValues().tab_index() != Rml::Style::TabIndex::None )
			a_rOut.push_back( pChild );
		else
			CollectFocusable( pChild, a_rOut );
	}
}

RmlFocusGroup::RmlFocusGroup( const Rml::String& a_rTag )
    : Rml::Element( a_rTag )
{
	AddEventListener( Rml::EventId::Mouseover, this );
	// Focus does not bubble, so catch it in the capture phase; click and keydown bubble
	AddEventListener( Rml::EventId::Focus, this, true );
	AddEventListener( Rml::EventId::Click, this );
	AddEventListener( Rml::EventId::Keydown, this );
}

RmlFocusGroup::~RmlFocusGroup()
{
	RemoveEventListener( Rml::EventId::Mouseover, this );
	RemoveEventListener( Rml::EventId::Focus, this, true );
	RemoveEventListener( Rml::EventId::Click, this );
	RemoveEventListener( Rml::EventId::Keydown, this );
}

void RmlFocusGroup::PlayCue( Rml::Element* a_pElement, const Rml::String& a_rAttribute )
{
	if ( !a_pElement )
		return;

	TINT iCue = a_pElement->GetAttribute<int>( a_rAttribute, -1 );
	if ( iCue < 0 )
		iCue = GetAttribute<int>( a_rAttribute, -1 );

	if ( iCue >= 0 && ASoundManager::IsSingletonCreated() )
		ASoundManager::GetSingleton()->PlayCue( iCue );
}

void RmlFocusGroup::SoundSelection( Rml::Element* a_pElement )
{
	if ( !a_pElement || a_pElement == m_pLastSounded )
		return;

	m_pLastSounded = a_pElement;

	// Track the element even while muted (menu open) so its first hover is silent
	if ( !s_bSuppressSounds )
		PlayCue( a_pElement, "sound-focus" );
}

void RmlFocusGroup::SpatialNavigate( Rml::Element* a_pCurrent, TINT a_iKey )
{
	if ( !a_pCurrent )
		return;

	// Manual override: nav-<dir>: #id on the focused element wins over the search
	Rml::PropertyId eNavId = Rml::PropertyId::NavUp;
	switch ( a_iKey )
	{
		case Rml::Input::KI_UP:    eNavId = Rml::PropertyId::NavUp; break;
		case Rml::Input::KI_DOWN:  eNavId = Rml::PropertyId::NavDown; break;
		case Rml::Input::KI_LEFT:  eNavId = Rml::PropertyId::NavLeft; break;
		case Rml::Input::KI_RIGHT: eNavId = Rml::PropertyId::NavRight; break;
		default:                   return;
	}

	if ( const Rml::Property* pNav = a_pCurrent->GetLocalProperty( eNavId ) )
	{
		if ( pNav->unit == Rml::Unit::STRING )
		{
			const Rml::String sValue = pNav->Get<Rml::String>();
			if ( sValue.size() > 1 && sValue[ 0 ] == '#' )
			{
				if ( Rml::Element* pTarget = GetElementById( sValue.substr( 1 ) ) )
				{
					pTarget->Focus();
					pTarget->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
				}
				return;
			}
		}
	}

	Rml::Vector<Rml::Element*> vecFocusable;
	CollectFocusable( this, vecFocusable );

	// Walk the focusables in document order (which matches the visual top-to-bottom,
	// left-to-right layout here). Up/down step to the next element on a different row;
	// left/right step to an adjacent element on the same row. Rows are grouped by
	// vertical overlap, which -- unlike RmlUi's position-based nav -- is not thrown off
	// by the scroll containers or the right-aligned controls vs centred tabs/buttons
	TINT iCur = -1;
	for ( TINT i = 0; i < TINT( vecFocusable.size() ); i++ )
		if ( vecFocusable[ i ] == a_pCurrent ) { iCur = i; break; }
	if ( iCur < 0 )
		return;

	const Rml::Vector2f vCurPos  = a_pCurrent->GetAbsoluteOffset( Rml::BoxArea::Border );
	const Rml::Vector2f vCurSize = a_pCurrent->GetBox().GetSize( Rml::BoxArea::Border );
	const float         fCurTop  = vCurPos.y;
	const float         fCurBot  = vCurPos.y + vCurSize.y;

	const TBOOL bForward  = ( a_iKey == Rml::Input::KI_DOWN || a_iKey == Rml::Input::KI_RIGHT );
	const TBOOL bVertical = ( a_iKey == Rml::Input::KI_UP || a_iKey == Rml::Input::KI_DOWN );
	const TINT  iStep     = bForward ? 1 : -1;

	Rml::Element* pBest = TNULL;
	for ( TINT i = iCur + iStep; i >= 0 && i < TINT( vecFocusable.size() ); i += iStep )
	{
		Rml::Element*       pCand = vecFocusable[ i ];
		const Rml::Vector2f vPos  = pCand->GetAbsoluteOffset( Rml::BoxArea::Border );
		const Rml::Vector2f vSize = pCand->GetBox().GetSize( Rml::BoxArea::Border );
		const float         fTop  = vPos.y;
		const float         fBot  = vPos.y + vSize.y;

		const float fOverlap = ( fCurBot < fBot ? fCurBot : fBot ) - ( fCurTop > fTop ? fCurTop : fTop );
		const TBOOL bSameRow = fOverlap > 0.0f;

		if ( bVertical )
		{
			// Skip the current row's horizontal neighbours; take the next row
			if ( !bSameRow )
			{
				pBest = pCand;
				break;
			}
		}
		else
		{
			// Adjacent element in this direction, only if it shares the row
			if ( bSameRow )
				pBest = pCand;
			break;
		}
	}

	// The walk lands on whichever element of the target row comes first in the travel
	// direction, so moving up hits the far end of a tab or button bar. Enter the row at
	// its selected element (a tabset's active tab) or, failing that, at its first one
	if ( pBest && bVertical )
	{
		const Rml::Vector2f vRowPos  = pBest->GetAbsoluteOffset( Rml::BoxArea::Border );
		const Rml::Vector2f vRowSize = pBest->GetBox().GetSize( Rml::BoxArea::Border );
		const float         fRowTop  = vRowPos.y;
		const float         fRowBot  = vRowPos.y + vRowSize.y;

		Rml::Element* pInRow = TNULL;

		for ( Rml::Element* pCand : vecFocusable )
		{
			// A scrolled panel keeps laying its rows out past the visible area, so only
			// siblings of the landing element count as its row
			if ( pCand->GetParentNode() != pBest->GetParentNode() )
				continue;

			const Rml::Vector2f vPos  = pCand->GetAbsoluteOffset( Rml::BoxArea::Border );
			const Rml::Vector2f vSize = pCand->GetBox().GetSize( Rml::BoxArea::Border );

			const float fTop     = vPos.y;
			const float fBot     = vPos.y + vSize.y;
			const float fOverlap = ( fRowBot < fBot ? fRowBot : fBot ) - ( fRowTop > fTop ? fRowTop : fTop );
			if ( fOverlap <= 0.0f )
				continue;

			if ( pCand->IsPseudoClassSet( "selected" ) )
			{
				pInRow = pCand;
				break;
			}

			if ( !pInRow )
				pInRow = pCand;
		}

		if ( pInRow )
			pBest = pInRow;
	}

	if ( pBest )
	{
		pBest->Focus();
		pBest->ScrollIntoView( Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Nearest ) );
	}
}

void RmlFocusGroup::ProcessEvent( Rml::Event& a_rEvent )
{
	const Rml::EventId eId = a_rEvent.GetId();

	// Keyboard nav sounds here; mouse hover sounds from the Mouseover branch. Both
	// share SoundSelection's de-dup, so a hover that moves focus never doubles up
	if ( eId == Rml::EventId::Focus )
	{
		Rml::Element* pTarget = a_rEvent.GetTargetElement();
		if ( pTarget && pTarget->GetComputedValues().tab_index() != Rml::Style::TabIndex::None )
			SoundSelection( pTarget );
		return;
	}

	if ( eId == Rml::EventId::Click )
	{
		PlayCue( a_rEvent.GetTargetElement(), "sound-click" );
		return;
	}

	if ( eId == Rml::EventId::Keydown )
	{
		// Opt-in cross-scroll-container arrow nav. A closed select releases up/down
		// (nav: auto) so it bubbles here; an open one consumes it in the capture phase
		if ( HasAttribute( "nav-spatial" ) )
		{
			const TINT iKey = a_rEvent.GetParameter<int>( "key_identifier", Rml::Input::KI_UNKNOWN );
			if ( iKey == Rml::Input::KI_UP || iKey == Rml::Input::KI_DOWN || iKey == Rml::Input::KI_LEFT || iKey == Rml::Input::KI_RIGHT )
			{
				SpatialNavigate( a_rEvent.GetTargetElement(), iKey );
				a_rEvent.StopPropagation();
			}
		}
		return;
	}

	if ( eId != Rml::EventId::Mouseover )
		return;

	// RmlUi re-runs the hover chain in Update() as elements slide under a stationary
	// cursor, so ignore hover-driven selection while an animation has input locked --
	// otherwise the appear/disappear would change the selection and play cues
	if ( remaster::rml::IsInputLocked() )
		return;

	// Find the hovered item that can accept focus: a focusable element directly, or
	// the nearest ancestor wrapping a single control (a settings row), so hovering
	// the row label counts too. Sound it, then focus it
	for ( Rml::Element* pElement = a_rEvent.GetTargetElement(); pElement && pElement != this; pElement = pElement->GetParentNode() )
	{
		Rml::Element* pControl = TNULL;

		if ( pElement->GetComputedValues().tab_index() != Rml::Style::TabIndex::None )
		{
			pControl = pElement;
		}
		else
		{
			Rml::Vector<Rml::Element*> vecFocusable;
			CollectFocusable( pElement, vecFocusable );
			if ( vecFocusable.size() == 1 )
				pControl = vecFocusable[ 0 ];
		}

		if ( pControl )
		{
			// Sound before focusing so the focus event de-dups against this element
			SoundSelection( pControl );
			pControl->Focus();
			return;
		}
	}
}

static Rml::ElementInstancerGeneric<RmlFocusGroup> s_oFocusGroupInstancer;

void RegisterFocusGroup()
{
	Rml::Factory::RegisterElementInstancer( "focusgroup", &s_oFocusGroupInstancer );
}

Rml::String GetFocusedElementId( Rml::ElementDocument* a_pDocument )
{
	if ( !a_pDocument )
		return Rml::String();

	// The document is its own focus leaf when nothing inside it is focused
	Rml::Element* pFocused = a_pDocument->GetFocusLeafNode();
	if ( !pFocused || pFocused == a_pDocument )
		return Rml::String();

	return pFocused->GetId();
}

TBOOL FocusFirstFocusable( Rml::Element* a_pRoot )
{
	if ( !a_pRoot )
		return TFALSE;

	Rml::Vector<Rml::Element*> vecFocusable;
	CollectFocusable( a_pRoot, vecFocusable );
	if ( vecFocusable.empty() )
		return TFALSE;

	RmlFocusGroup::SuppressSounds( TTRUE );
	vecFocusable[ 0 ]->Focus();
	RmlFocusGroup::SuppressSounds( TFALSE );
	return TTRUE;
}

void RestoreFocusedElement( Rml::ElementDocument* a_pDocument, const Rml::String& a_rId, const TCHAR* a_szFallbackId )
{
	if ( !a_pDocument )
		return;

	Rml::Element* pTarget = a_rId.empty() ? TNULL : a_pDocument->GetElementById( a_rId );
	if ( !pTarget && a_szFallbackId )
		pTarget = a_pDocument->GetElementById( a_szFallbackId );

	// No named fallback (or it is gone): start at the top of the menu, whatever the
	// layout currently lists first
	if ( !pTarget )
	{
		FocusFirstFocusable( a_pDocument );
		return;
	}

	RmlFocusGroup::SuppressSounds( TTRUE );
	pTarget->Focus();
	RmlFocusGroup::SuppressSounds( TFALSE );
}

} // namespace remaster
