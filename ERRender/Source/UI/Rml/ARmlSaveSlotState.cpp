#include "pch.h"
#include "ARmlSaveSlotState.h"
#include "ARmlConfirmPopupState.h"
#include "RmlManager.h"
#include "RmlFocusGroup.h"

#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/AGUI2.h>
#include <BYardSDK/ASoundManager.h>
#include <BYardSDK/ALocaleManager.h>
#include <BYardSDK/APlayerProgress.h>
#include <BYardSDK/APlayerProfileManager.h>
#include <BYardSDK/ASaveSlot.h>

#include <Input/TInputDeviceKeyboard.h>
#include <Toshi/T2String8.h>

#include <RmlUi/Core.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_NORUNTIME( ARmlSaveSlotState );

namespace
{

// 1283 is the retail "empty slot" label. It resolves to "<Empty>", so it must be
// bound as data (set as text) rather than injected via a [[N]] marker, whose
// angle brackets would be parsed as markup
constexpr TINT LOCALE_EMPTY_SLOT = 1283;

struct SaveSlotItem
{
	bool        occupied;
	Rml::String name;
	Rml::String date;
	Rml::String hint;
	Rml::String portrait;   // decorator string selecting the character's mug sprite
};

Rml::Vector<SaveSlotItem> s_vecSlots;
bool                      s_bIsNew      = true;
bool                      s_bHasSaves   = false;
bool                      s_bModelReady = false;
Rml::DataModelHandle      s_hModel;

ARmlSaveSlotState::EMode  s_eMode     = ARmlSaveSlotState::MODE_NEW;

enum EPending
{
	PENDING_NONE,
	PENDING_SELECT,
	PENDING_CONFIRM_OVERWRITE,
	PENDING_CANCEL,
};
EPending s_ePending     = PENDING_NONE;
TINT     s_iPendingSlot = -1;

static Rml::String LocalizedUtf8( TINT a_iId )
{
	if ( !ALocaleManager::IsSingletonCreated() )
		return Rml::String();

	const TWCHAR* pwStr = ALocaleManager::GetSingleton()->GetString( a_iId );
	if ( !pwStr )
		return Rml::String();

	TCHAR szUtf8[ 0x80 ] = {};
	WideCharToMultiByte( CP_UTF8, 0, pwStr, -1, szUtf8, sizeof( szUtf8 ), TNULL, TNULL );
	return Rml::String( szUtf8 );
}

static Rml::String ReadSlotName( const ASaveSlot* a_pSlot )
{
	const TWCHAR* pwName = a_pSlot->GetName();

	TWCHAR awName[ 0x21 ];
	TINT   iLen = 0;
	for ( ; iLen < 0x20 && pwName[ iLen ] != 0; iLen++ )
		awName[ iLen ] = pwName[ iLen ];
	awName[ iLen ] = 0;

	if ( iLen == 0 )
		T2String16::Copy( awName, L"Unnamed" );

	TCHAR szUtf8[ 0x60 ] = {};
	WideCharToMultiByte( CP_UTF8, 0, awName, -1, szUtf8, sizeof( szUtf8 ), TNULL, TNULL );
	return Rml::String( szUtf8 );
}

// Selects the mug sprite for the slot's character. Gender/costume may be sentinels
// (2 / -1) meaning "use the current profile", matching the retail portrait builder
static Rml::String BuildPortrait( const ASaveSlot* a_pSlot )
{
	TINT iGender  = a_pSlot->GetGender();
	TINT iCostume = a_pSlot->GetSkinIndex();

	if ( ( iGender == 2 || iCostume == -1 ) && APlayerProfileManager::IsSingletonCreated() )
	{
		APlayerProfileManager* pMgr = APlayerProfileManager::GetSingleton();
		if ( iGender == 2 )
			iGender = pMgr->GetMainGender();
		if ( iCostume == -1 )
			iCostume = pMgr->GetMainCostume();
	}

	if ( iCostume < 0 || iCostume > 6 )
		iCostume = 0;

	TCHAR szDecorator[ 48 ];
	T2String8::Format( szDecorator, sizeof( szDecorator ), "image( mug-%s-%d )",
	                   ( iGender == 0 ) ? "man" : "girl", iCostume );
	return Rml::String( szDecorator );
}

static Rml::String ReadSlotDate( const ASaveSlot* a_pSlot )
{
	TCHAR szDate[ 32 ];
	T2String8::Format( szDate, sizeof( szDate ), "%02u.%02u.%02u, %02u:%02u",
	                   a_pSlot->GetDay(), a_pSlot->GetMonth() + 1, a_pSlot->GetYear() % 100,
	                   a_pSlot->GetHour(), a_pSlot->GetMinute() );
	return Rml::String( szDate );
}

static void ReadSlots()
{
	s_vecSlots.clear();
	s_bHasSaves = false;

	APlayerProgress* pProgress = APlayerProgress::GetSingleton();

	TBOOL bFirstEmptyHinted = TFALSE;

	for ( TINT i = 0; i < APlayerProgress::SLOT_COUNT; i++ )
	{
		SaveSlotItem oItem;
		oItem.occupied = false;

		ASaveSlot* pSlot = pProgress ? pProgress->GetSaveSlot( i ) : TNULL;
		if ( pSlot && pSlot->IsOccupied() )
		{
			oItem.occupied = true;
			oItem.name     = ReadSlotName( pSlot );
			oItem.date     = ReadSlotDate( pSlot );
			oItem.portrait = BuildPortrait( pSlot );
			s_bHasSaves    = true;
		}

		if ( !oItem.occupied )
		{
			oItem.portrait = "none";
			oItem.name     = LocalizedUtf8( LOCALE_EMPTY_SLOT );

			// Hint only the first empty slot, and only when starting a new game
			if ( s_eMode == ARmlSaveSlotState::MODE_NEW && !bFirstEmptyHinted )
			{
				oItem.hint        = "Press Enter to start a new game";
				bFirstEmptyHinted = TTRUE;
			}
		}

		s_vecSlots.push_back( oItem );
	}
}

static void OnSelectSlot( TINT a_iSlot )
{
	if ( a_iSlot < 0 || a_iSlot >= TINT( s_vecSlots.size() ) )
		return;

	const TBOOL bOccupied = s_vecSlots[ a_iSlot ].occupied;

	// Nothing to load from an empty slot
	if ( s_eMode == ARmlSaveSlotState::MODE_LOAD && !bOccupied )
		return;

	s_iPendingSlot = a_iSlot;

	// Starting a new game over an existing save asks first, as retail does
	s_ePending = ( s_eMode == ARmlSaveSlotState::MODE_NEW && bOccupied ) ? PENDING_CONFIRM_OVERWRITE : PENDING_SELECT;
}

// Mirrors AFrontEndNewGameSelectState's empty-slot branch: pick the slot, wipe it if
// it held a save, then boot a fresh game
static void StartNewGame( TINT a_iSlot, TBOOL a_bOccupied )
{
	APlayerProgress* pProgress = APlayerProgress::GetSingleton();
	pProgress->SetActiveSlot( a_iSlot );

	if ( a_bOccupied )
		pProgress->ClearActiveSlot();

	pProgress->StartNewGame();
}

// Mirrors AFrontEndLoadState's confirm handler: pick the slot, then load its save
static void LoadGame( TINT a_iSlot )
{
	APlayerProgress* pProgress = APlayerProgress::GetSingleton();
	pProgress->SetActiveSlot( a_iSlot );
	pProgress->LoadGame();
}

// Retail overwrite dialog strings: AMessagePopupState( GetString(0x428), GetString(0x121) )
constexpr TINT LOCALE_OVERWRITE_TITLE   = 1064;
constexpr TINT LOCALE_OVERWRITE_MESSAGE = 289;

// Survives the popup, which clears the menu's own pending slot
TINT s_iOverwriteSlot = -1;

static void ConfirmOverwrite()
{
	StartNewGame( s_iOverwriteSlot, TTRUE );
}

} // namespace

ARmlSaveSlotState::ARmlSaveSlotState( EMode a_eMode )
{
	m_eMode     = a_eMode;
	m_pDocument = TNULL;
}

void ARmlSaveSlotState::OnActivate()
{
	// This instance persists on the stack while the overwrite popup runs, so clear any
	// leftover close state when the menu comes back
	s_eMode    = m_eMode;
	s_bIsNew   = ( m_eMode == MODE_NEW );
	s_ePending = PENDING_NONE;
	m_bFocused   = TFALSE;
	m_bActed     = TFALSE;
	m_bClosing   = TFALSE;
	m_fCloseTime = 0.0f;

	remaster::rml::SetEnabled( TTRUE );
	remaster::rml::LockInput( remaster::rml::APPEAR_ANIMATION_DURATION );

	ReadSlots();

	Rml::Context* pContext = remaster::rml::GetContext();
	if ( !pContext )
		return;

	if ( !s_bModelReady )
	{
		Rml::DataModelConstructor oModel = pContext->CreateDataModel( "saveslots" );

		if ( auto oStruct = oModel.RegisterStruct<SaveSlotItem>() )
		{
			oStruct.RegisterMember( "occupied", &SaveSlotItem::occupied );
			oStruct.RegisterMember( "name", &SaveSlotItem::name );
			oStruct.RegisterMember( "date", &SaveSlotItem::date );
			oStruct.RegisterMember( "hint", &SaveSlotItem::hint );
			oStruct.RegisterMember( "portrait", &SaveSlotItem::portrait );
		}
		oModel.RegisterArray<Rml::Vector<SaveSlotItem>>();

		oModel.Bind( "slots", &s_vecSlots );
		oModel.Bind( "isNew", &s_bIsNew );
		oModel.Bind( "hasSaves", &s_bHasSaves );

		oModel.BindEventCallback( "select", []( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a_rArgs ) {
			if ( !a_rArgs.empty() )
				OnSelectSlot( a_rArgs[ 0 ].Get<int>( -1 ) );
		} );

		s_hModel      = oModel.GetModelHandle();
		s_bModelReady = true;
	}
	else
	{
		s_hModel.DirtyAllVariables();
	}

	m_pDocument = remaster::rml::LoadDocument( "Data/UI/Layout/saveslots.rml" );
	remaster::rml::ShowDocument( m_pDocument );

	AGUI2::GetSingleton()->SetCursorVisible( TTRUE );
}

void ARmlSaveSlotState::OnDeactivate()
{
	AGUI2::GetSingleton()->SetCursorVisible( TFALSE );

	remaster::rml::CloseDocument( m_pDocument );
	m_pDocument = TNULL;

	remaster::rml::SetEnabled( TFALSE );
}

TBOOL ARmlSaveSlotState::OnUpdate( TFLOAT a_fDeltaTime )
{
	if ( m_bActed )
		return TTRUE;

	// The data-for slots are generated on the context's first update, so focus the
	// first one once it exists (silently, so it does not sound on open)
	if ( !m_bFocused && m_pDocument )
	{
		if ( Rml::Element* pList = m_pDocument->GetElementById( "slots" ) )
		{
			const TINT iNumSlots = pList->GetNumChildren();
			if ( iNumSlots > 0 )
			{
				// Load hides the empty slots, so start on the first one still shown
				for ( TINT i = 0; i < iNumSlots; i++ )
				{
					Rml::Element* pSlot = pList->GetChild( i );
					if ( !pSlot->IsVisible() )
						continue;

					remaster::RmlFocusGroup::SuppressSounds( TTRUE );
					pSlot->Focus();
					remaster::RmlFocusGroup::SuppressSounds( TFALSE );
					break;
				}

				m_bFocused = TTRUE;
			}
		}
	}

	if ( s_ePending != PENDING_NONE && !m_bClosing )
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
			m_bClosing = TFALSE;
			m_bActed   = TTRUE;

			const EPending ePending  = s_ePending;
			const TINT     iSlot     = s_iPendingSlot;
			const TBOOL    bOccupied = ( iSlot >= 0 && iSlot < TINT( s_vecSlots.size() ) ) ? s_vecSlots[ iSlot ].occupied : TFALSE;
			s_ePending     = PENDING_NONE;
			s_iPendingSlot = -1;

			AGameStateController* pController = AGameStateController::GetSingleton();

			if ( ePending == PENDING_CONFIRM_OVERWRITE )
			{
				s_iOverwriteSlot = iSlot;
				pController->PushState( new ARmlConfirmPopupState( LOCALE_OVERWRITE_TITLE, LOCALE_OVERWRITE_MESSAGE, ConfirmOverwrite ) );
			}
			else if ( ePending == PENDING_SELECT && s_eMode == MODE_NEW )
			{
				// The boot transition unwinds the front-end stack itself, like retail
				StartNewGame( iSlot, bOccupied );
			}
			else if ( ePending == PENDING_SELECT && s_eMode == MODE_LOAD )
			{
				pController->PopCurrentGameState();
				LoadGame( iSlot );
			}
			else
			{
				pController->PopCurrentGameState();
			}
		}
	}

	return TTRUE;
}

TBOOL ARmlSaveSlotState::ProcessInput( const TInputInterface::InputEvent* a_pInputEvent )
{
	if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN &&
	     a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_ESCAPE )
	{
		s_ePending = PENDING_CANCEL;
	}

	return TTRUE;
}
