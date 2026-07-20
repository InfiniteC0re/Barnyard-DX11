#include "pch.h"
#include "BootState.h"
#include "RenderDX11.h"
#include "RenderParams.h"

#include <BYardSDK/SDK_T2GUIFont.h>
#include <BYardSDK/SDK_T2GUIFontManager.h>
#include <BYardSDK/AGUI2TextBox.h>
#include <BYardSDK/AGUI2.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static void SetSaveLoadBackgroundVisible( TBOOL a_bVisible )
{
	TUINT8* pSaveLoadSKU = *(TUINT8**)0x007b5edc;
	if ( !pSaveLoadSKU )
		return;

	SDK_T2GUIElement* pBackgroundRect = *(SDK_T2GUIElement**)( pSaveLoadSKU + 0xB4 );
	if ( !pBackgroundRect )
		return;

	if ( a_bVisible )
		pBackgroundRect->Show();
	else
		pBackgroundRect->Hide();
}

TBOOL BootState::Update()
{
	static AGUI2TextBox* s_pWarmupText  = TNULL;
	static AGUI2TextBox* s_pWarmupNote  = TNULL;

	if ( !remaster::ShaderWarmup_IsComplete() )
	{
		// With a warm shader cache the warm-up finishes within a few frames; don't flash the boot screen
		static ULONGLONG s_uiFirstUpdateTicks = GetTickCount64();
		if ( GetTickCount64() - s_uiFirstUpdateTicks > 400 )
			remaster::g_bBootScreenVisible = TTRUE;

		if ( remaster::g_bBootScreenVisible )
			SetSaveLoadBackgroundVisible( TFALSE );

		if ( remaster::g_bBootScreenVisible && !s_pWarmupText )
		{
			if ( SDK_T2GUIFont* pFont = SDK_T2GUIFontManager::FindFont( "Rekord18" ) )
			{
				s_pWarmupText = AGUI2TextBox::CreateFromEngine();
				s_pWarmupText->Create( pFont, 600.0f );
				s_pWarmupText->SetAttachment( SDK_T2GUIElement::Anchor_BottomCenter, SDK_T2GUIElement::Pivot_BottomCenter );
				s_pWarmupText->SetTransform( 0.0f, -40.0f );
				s_pWarmupText->SetInFront();
				AGUI2::GetRootElement()->AddChildTail( *s_pWarmupText );

				s_pWarmupNote = AGUI2TextBox::CreateFromEngine();
				s_pWarmupNote->Create( pFont, 600.0f );
				s_pWarmupNote->SetAttachment( SDK_T2GUIElement::Anchor_BottomCenter, SDK_T2GUIElement::Pivot_BottomCenter );
				s_pWarmupNote->SetTransform( 0.0f, -20.0f );
				s_pWarmupNote->SetScale( 0.65f );
				s_pWarmupNote->SetAlpha( 0.5f );
				s_pWarmupNote->SetInFront();
				s_pWarmupNote->SetText( L"This process will only happen once" );
				AGUI2::GetRootElement()->AddChildTail( *s_pWarmupNote );
			}
		}

		// Finalize families the worker thread has compiled so far; cheap, keeps this frame rendering
		if ( !remaster::ShaderWarmup_RunStep() )
		{
			if ( s_pWarmupText )
			{
				static wchar_t s_wszProgress[ 96 ];
				TINT           iDone, iTotal;
				remaster::ShaderWarmup_GetProgress( iDone, iTotal );
				const TINT iPercent = ( iTotal > 0 ) ? iDone * 100 / iTotal : 0;
				Toshi::TStringManager::String16Format( s_wszProgress, TARRAYSIZE( s_wszProgress ), L"Compiling shaders...\n\n%d%%", iPercent );
				s_pWarmupText->SetText( s_wszProgress );
			}

			return TTRUE;
		}

		if ( s_pWarmupText )
		{
			s_pWarmupText->Hide();
			s_pWarmupText->Unlink();
		}

		if ( s_pWarmupNote )
		{
			s_pWarmupNote->Hide();
			s_pWarmupNote->Unlink();
		}

		if ( remaster::g_bBootScreenVisible )
		{
			SetSaveLoadBackgroundVisible( TTRUE );
			remaster::g_bBootScreenVisible = TFALSE;
		}
	}

	return TFALSE;
}
