#include "pch.h"
#include "Editor.h"
#include "Settings.h"
#include "DynamicGlowLights.h"

using namespace remaster;

#include <AHooks.h>
#include <AImGui.h>
#include <HookHelpers.h>

#include <Input/TInputDeviceKeyboard.h>
#include <Math/TMatrix44.h>
#include <Render/TRenderContext.h>

#include <BYardSDK/AGameStateController.h>
#include <BYardSDK/AGUI2.h>
#include <BYardSDK/ARootTask.h>
#include <BYardSDK/AGlowViewport.h>
#include <BYardSDK/ACamera.h>

#include <imgui.h>
#include "ImGuizmo/ImGuizmo.h"
#include "RenderDX11.h"
#include "RenderContentDX11.h"

#include <BYardSDK/ARenderer.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace editor
{

bool g_bEnabled = TFALSE;

// ============================================================
// Dynamic Light Manager
// ============================================================

struct EditorLight
{
	char   szName[ 64 ];
	TFLOAT vPosition[ 3 ];  // world-space XYZ
	TFLOAT flAzimuth;       // horizontal angle, degrees: 0=+Z, 90=+X
	TFLOAT flElevation;     // vertical angle, degrees: +90=straight up, -90=straight down
	TFLOAT flFOV;           // full cone angle, degrees
	TFLOAT flRange;         // far clip / influence radius

	TBOOL bEnabled;
	TBOOL bNightOnly;

	// Per-light rendering settings -- written to g_aGlowLightSettings each frame.
	GlowLightSettings settings;

	AGlowViewport::GlowObject* pGlowObject;
};

static constexpr TINT MAX_EDITOR_LIGHTS = remaster::DYNAMIC_GLOW_LIGHT_COUNT;
static EditorLight    s_aLights[ MAX_EDITOR_LIGHTS ];
static TINT           s_iNumLights = 0;

// Builds a world-space TMatrix44 with FORWARD pointing along the given azimuth/elevation.
//   azimuth  : degrees, clockwise from +Z looking down (0=+Z, 90=+X)
//   elevation: degrees, +90=straight up, -90=straight down
static TMatrix44 BuildLightTransform( const TFLOAT pos[ 3 ], TFLOAT azimuthDeg, TFLOAT elevationDeg )
{
	const TFLOAT az = TMath::DegToRad( azimuthDeg );
	const TFLOAT el = TMath::DegToRad( elevationDeg );

	TVector4 forward(
	    TMath::Cos( el ) * TMath::Sin( az ),
	    TMath::Sin( el ),
	    TMath::Cos( el ) * TMath::Cos( az ),
	    0.0f
	);

	// Choose a stable world-up reference; fall back to +Z when forward is nearly vertical
	TVector4 worldUp = ( TMath::Abs( forward.y ) > 0.99f )
	    ? TVector4( 0.0f, 0.0f, 1.0f, 0.0f )
	    : TVector4( 0.0f, 1.0f, 0.0f, 0.0f );

	// right = worldUp x forward
	TVector4 right(
	    worldUp.y * forward.z - worldUp.z * forward.y,
	    worldUp.z * forward.x - worldUp.x * forward.z,
	    worldUp.x * forward.y - worldUp.y * forward.x,
	    0.0f
	);
	right.Normalise();

	// up = forward x right
	TVector4 up(
	    forward.y * right.z - forward.z * right.y,
	    forward.z * right.x - forward.x * right.z,
	    forward.x * right.y - forward.y * right.x,
	    0.0f
	);
	up.Normalise();

	TMatrix44 mat;
	mat.AsBasisVector4( BASISVECTOR_RIGHT )       = right;
	mat.AsBasisVector4( BASISVECTOR_UP )          = up;
	mat.AsBasisVector4( BASISVECTOR_FORWARD )     = forward;
	mat.AsBasisVector4( BASISVECTOR_TRANSLATION ) = TVector4( pos[ 0 ], pos[ 1 ], pos[ 2 ], 1.0f );
	return mat;
}

// Computes viewport + projection params that produce a cone with the given full FOV angle.
// Uses a 2x2 virtual viewport so that (fWidth * 0.25 = 0.5) and:
//   tan(halfFOV) = 0.5 / m_Proj.x  ->  m_Proj.x = 0.5 / tan(halfFOV)
static void BuildLightProjection(
    TRenderContext::VIEWPORTPARAMS&   a_rVP,
    TRenderContext::PROJECTIONPARAMS& a_rPP,
    TFLOAT                            a_flFOVDeg,
    TFLOAT                            a_flRange
)
{
	a_rVP.fX     = 0.0f;
	a_rVP.fY     = 0.0f;
	a_rVP.fWidth  = 2.0f;
	a_rVP.fHeight = 2.0f;
	a_rVP.fMinZ  = 0.0f;
	a_rVP.fMaxZ  = 1.0f;

	const TFLOAT fHalfFOV = TMath::DegToRad( a_flFOVDeg ) * 0.5f;

	a_rPP.m_Proj.x    = a_rPP.m_Proj.y    = 0.5f / TMath::Max( TMath::Tan( fHalfFOV ), 0.001f );
	a_rPP.m_Centre.x  = a_rPP.m_Centre.y  = 1.0f;
	a_rPP.m_fNearClip = 0.1f;
	a_rPP.m_fFarClip  = TMath::Max( a_flRange, 1.0f );
}

static void ApplyLightToGlowObject( EditorLight& a_rLight )
{
	if ( !a_rLight.pGlowObject )
		return;

	TMatrix44 oTransform = BuildLightTransform( a_rLight.vPosition, a_rLight.flAzimuth, a_rLight.flElevation );

	TRenderContext::VIEWPORTPARAMS   vp;
	TRenderContext::PROJECTIONPARAMS pp;
	BuildLightProjection( vp, pp, a_rLight.flFOV, a_rLight.flRange );

	// Setup() internally inverts the matrix to transform frustum planes to world space,
	// so it expects the view matrix (world-to-light-space), not the world transform.
	TMatrix44 oViewMatrix;
	oViewMatrix.InvertOrthogonal( oTransform );

	a_rLight.pGlowObject->Setup( oViewMatrix, vp, pp, TRenderContext::CameraMode_Perspective );

	// Store the world transform separately -- BuildGlowObjectWorldTransform reads m_oTransform
	// directly for editor lights (those with m_pSceneObject == TNULL).
	a_rLight.pGlowObject->m_oTransform    = oTransform;
	a_rLight.pGlowObject->m_pSceneObject  = TNULL;
	a_rLight.pGlowObject->m_bEnabled      = a_rLight.bEnabled;
	a_rLight.pGlowObject->m_bIsNightLight = a_rLight.bNightOnly;

	// Register per-light rendering settings so the upload path uses them instead
	// of the global defaults.
	const Toshi::TLightID iID = a_rLight.pGlowObject->m_iID;
	if ( iID >= 0 && iID < MAX_GLOW_LIGHT_SETTINGS )
	{
		a_rLight.settings.bOverride = TTRUE;
		g_aGlowLightSettings[ iID ] = a_rLight.settings;
	}
}

static void AddLight()
{
	if ( s_iNumLights >= MAX_EDITOR_LIGHTS )
		return;

	AGlowViewport* pVP = AGlowViewport::GetSingleton();
	if ( !pVP )
		return;

	AGlowViewport::GlowObject* pObj = pVP->CreateGlowObject();
	if ( !pObj )
		return;

	EditorLight& light = s_aLights[ s_iNumLights ];
	TUtil::MemClear( &light, sizeof( light ) );

	_snprintf_s( light.szName, sizeof( light.szName ), _TRUNCATE, "Light %d", s_iNumLights + 1 );
	light.pGlowObject = pObj;
	light.flAzimuth   = 0.0f;
	light.flElevation = -90.0f; // point straight down by default
	light.flFOV       = 60.0f;
	light.flRange     = 20.0f;
	light.bEnabled    = TTRUE;
	light.bNightOnly  = TFALSE;

	// Initialise per-light settings from current global defaults.
	light.settings                    = GetGlowLightSettings( -1 ); // -1 always returns defaults
	light.settings.bOverride          = TTRUE;

	ApplyLightToGlowObject( light );
	s_iNumLights++;
}

static void RemoveLight( TINT a_iIndex )
{
	if ( a_iIndex < 0 || a_iIndex >= s_iNumLights )
		return;

	AGlowViewport* pVP = AGlowViewport::GetSingleton();

	if ( s_aLights[ a_iIndex ].pGlowObject )
	{
		// Clear per-light settings so the slot reverts to global defaults.
		const Toshi::TLightID iID = s_aLights[ a_iIndex ].pGlowObject->m_iID;
		if ( iID >= 0 && iID < MAX_GLOW_LIGHT_SETTINGS )
			TUtil::MemClear( &g_aGlowLightSettings[ iID ], sizeof( GlowLightSettings ) );

		if ( pVP )
			pVP->RemoveGlowObject( s_aLights[ a_iIndex ].pGlowObject );
	}

	// Shift remaining entries down
	for ( TINT i = a_iIndex; i < s_iNumLights - 1; i++ )
		s_aLights[ i ] = s_aLights[ i + 1 ];

	s_iNumLights--;
	TUtil::MemClear( &s_aLights[ s_iNumLights ], sizeof( EditorLight ) );
}

// ============================================================
// Input hook
// ============================================================

// True while this module is holding an ImGui input lock for a debug overlay.
static bool s_bDebugInputLocked = false;

// Acquires/releases the ImGui input lock exactly once to match whether any debug overlay
// is open. AImGUI's lock is a counter (m_iNumInputLocks); calling LockInput on every
// toggle would let it drift above zero and never release, leaving the game unable to
// receive input. Tracking our own held state keeps the counter balanced.
static void SyncDebugInputLock()
{
	const bool bWantLock = g_bEnabled || settings::g_bEnabled;
	if ( bWantLock == s_bDebugInputLocked )
		return;

	if ( bWantLock )
		g_pImGui->LockInput();
	else
		g_pImGui->UnlockInput();

	s_bDebugInputLocked = bWantLock;
}

MEMBER_HOOK( 0x004293d0, AGameStateController, AGameStateController_ProcessInput, TBOOL, TInputInterface::InputEvent* a_pInputEvent )
{
	if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN )
	{
		Toshi::TInputDeviceKeyboard* pKeyboard = TSTATICCAST( Toshi::TInputDeviceKeyboard, a_pInputEvent->GetSource() );

		if ( pKeyboard->IsAltDown() && a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_Z )
		{
			g_bEnabled = !g_bEnabled;
			if ( g_bEnabled ) settings::g_bEnabled = false; // only one debug overlay open at a time

			SyncDebugInputLock();
			return TTRUE;
		}

		if ( pKeyboard->IsAltDown() && a_pInputEvent->GetDoodad() == TInputDeviceKeyboard::KEY_G )
		{
			settings::g_bEnabled = !settings::g_bEnabled;
			if ( settings::g_bEnabled ) g_bEnabled = false; // only one debug overlay open at a time

			SyncDebugInputLock();
			return TTRUE;
		}
	}

	return CallOriginal( a_pInputEvent );
}

void SetupHooks()
{
	InstallHook<AGameStateController_ProcessInput>();
}

// ============================================================
// Gizmo state
// ============================================================

static TINT                  s_iSelectedLight = -1;
static ImGuizmo::OPERATION   s_eGizmoOp       = ImGuizmo::TRANSLATE;

// ============================================================
// Render (called from OnImGuiRender)
// ============================================================

// Builds camera view and projection matrices for ImGuizmo.
// Mirrors the pattern used by other rendering code in ERRenderWrapper:
//   - projection comes from ARenderer's viewport render context (already computed)
//   - view matrix is derived from ACamera's world transform (view-world), inverted
static bool BuildGizmoMatrices( TMatrix44& a_rView, TMatrix44& a_rProj )
{
	ARenderer* pRenderer = ARenderer::GetSingleton();
	if ( !pRenderer || !pRenderer->m_pViewport )
		return false;

	auto* pContext = TSTATICCAST( remaster::RenderContextD3D11, pRenderer->m_pViewport->GetRenderContext() );
	if ( !pContext )
		return false;

	// Projection is already built by the engine each frame.
	a_rProj = pContext->GetProjectionMatrix();

	// ACamera::m_Matrix is the view-world transform (camera's world-space pose).
	// Invert it to get the world-to-view matrix that ImGuizmo expects.
	TMatrix44 matViewWorld = pContext->GetViewWorldMatrix();
	if ( ACameraManager* pCamMgr = ACameraManager::GetSingleton() )
	{
		if ( ACamera* pCamera = pCamMgr->GetCurrentCamera() )
			matViewWorld = pCamera->m_Matrix;
	}

	a_rView.InvertOrthogonal( matViewWorld );
	return true;
}

void Render()
{
	if ( !g_bEnabled )
		return;

	// -----------------------------------------------------------------------
	// ImGuizmo frame setup -- must happen before any Manipulate() call
	// -----------------------------------------------------------------------
	ImGuizmo::BeginFrame();
	ImGuizmo::SetOrthographic( false );

	const ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect( 0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y );

	// -----------------------------------------------------------------------
	// Dynamic Light Manager panel
	// -----------------------------------------------------------------------
	ImGui::SetNextWindowSize( ImVec2( 390.0f, 540.0f ), ImGuiCond_FirstUseEver );
	ImGui::Begin( "Dynamic Light Manager" );

	// Gizmo operation selector
	ImGui::TextUnformatted( "Gizmo:" );
	ImGui::SameLine();
	if ( ImGui::RadioButton( "Translate", s_eGizmoOp == ImGuizmo::TRANSLATE ) ) s_eGizmoOp = ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	if ( ImGui::RadioButton( "Rotate",    s_eGizmoOp == ImGuizmo::ROTATE    ) ) s_eGizmoOp = ImGuizmo::ROTATE;

	ImGui::Separator();
	ImGui::Text( "Lights: %d / %d", s_iNumLights, MAX_EDITOR_LIGHTS );
	ImGui::SameLine();

	if ( s_iNumLights < MAX_EDITOR_LIGHTS )
	{
		if ( ImGui::Button( "Add Light" ) )
			AddLight();
	}
	else
	{
		ImGui::TextDisabled( "Add Light (max)" );
	}

	ImGui::Separator();

	TINT iToRemove = -1;

	for ( TINT i = 0; i < s_iNumLights; i++ )
	{
		EditorLight& light  = s_aLights[ i ];
		bool         bDirty = false;

		ImGui::PushID( i );

		// Tint the header orange when selected, red when disabled
		if ( i == s_iSelectedLight )
			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.6f, 0.35f, 0.0f, 1.0f ) );
		else if ( !light.bEnabled )
			ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.5f, 0.1f, 0.1f, 1.0f ) );

		const bool bOpen = ImGui::CollapsingHeader( light.szName );

		if ( i == s_iSelectedLight || !light.bEnabled )
			ImGui::PopStyleColor();

		// Select this light for gizmo manipulation on header click
		if ( ImGui::IsItemClicked() )
			s_iSelectedLight = ( s_iSelectedLight == i ) ? -1 : i;

		// Remove button on the same line as the header
		ImGui::SameLine( ImGui::GetWindowWidth() - 32.0f );
		if ( ImGui::SmallButton( "X" ) )
			iToRemove = i;

		if ( bOpen )
		{
			if ( ImGui::InputText( "Name##name", light.szName, sizeof( light.szName ) ) )
				bDirty = true;

			bool bEnabled = (bool)light.bEnabled;
			if ( ImGui::Checkbox( "Enabled", &bEnabled ) )
			{
				light.bEnabled = (TBOOL)bEnabled;
				bDirty = true;
			}

			bool bNight = (bool)light.bNightOnly;
			if ( ImGui::Checkbox( "Night Only", &bNight ) )
			{
				light.bNightOnly = (TBOOL)bNight;
				bDirty = true;
			}

			ImGui::Separator();
			ImGui::TextUnformatted( "Transform" );

			if ( ImGui::DragFloat3( "Position", light.vPosition, 0.1f ) )
				bDirty = true;

			if ( ImGui::DragFloat( "Azimuth", &light.flAzimuth, 1.0f, -360.0f, 360.0f, "%.1f deg" ) )
				bDirty = true;

			if ( ImGui::DragFloat( "Elevation", &light.flElevation, 1.0f, -90.0f, 90.0f, "%.1f deg" ) )
				bDirty = true;

			// Snap position + direction from the current camera transform
			ACamera* pCamera = ACameraManager::GetSingleton()
			    ? ACameraManager::GetSingleton()->GetCurrentCamera()
			    : TNULL;

			if ( pCamera )
			{
				if ( ImGui::Button( "Place at Camera" ) )
				{
					const TVector4& vPos = pCamera->m_Matrix.GetTranslation();
					const TVector4& vFwd = pCamera->m_Matrix.AsBasisVector4( BASISVECTOR_FORWARD );

					light.vPosition[ 0 ] = vPos.x;
					light.vPosition[ 1 ] = vPos.y;
					light.vPosition[ 2 ] = vPos.z;

					const TFLOAT fFwdY    = TMath::Max( -1.0f, TMath::Min( 1.0f, vFwd.y ) );
					light.flElevation = TMath::ASin( fFwdY ) * ( 180.0f / TMath::PI );
					light.flAzimuth   = TMath::ATan2( vFwd.x, vFwd.z ) * ( 180.0f / TMath::PI );

					bDirty = true;
				}
			}

			ImGui::Separator();
			ImGui::TextUnformatted( "Projection" );

			if ( ImGui::SliderFloat( "FOV", &light.flFOV, 5.0f, 170.0f, "%.1f deg" ) )
				bDirty = true;

			if ( ImGui::DragFloat( "Range", &light.flRange, 0.5f, 1.0f, 500.0f, "%.1f m" ) )
				bDirty = true;

			ImGui::Separator();
			ImGui::TextUnformatted( "Appearance" );

			if ( ImGui::ColorEdit3( "Color", light.settings.flColor ) )
				bDirty = true;

			if ( ImGui::SliderFloat( "Surface Intensity", &light.settings.flSurfaceIntensity, 0.0f, 10.0f, "%.3f" ) )
				bDirty = true;

			if ( ImGui::SliderFloat( "Volumetric Intensity", &light.settings.flVolumetricIntensity, 0.0f, 3.0f, "%.3f" ) )
				bDirty = true;

			if ( ImGui::SliderFloat( "Bump Scale", &light.settings.flBumpScale, 0.0f, 10.0f, "%.2f" ) )
				bDirty = true;

			ImGui::Separator();
			ImGui::TextUnformatted( "Flicker" );

			bool bFlicker = (bool)light.settings.bFlickerEnabled;
			if ( ImGui::Checkbox( "Flicker Enabled", &bFlicker ) )
			{
				light.settings.bFlickerEnabled = (TBOOL)bFlicker;
				bDirty = true;
			}

			if ( light.settings.bFlickerEnabled )
			{
				if ( ImGui::SliderFloat( "Flicker Speed", &light.settings.flFlickerSpeed, 0.5f, 30.0f, "%.1f" ) )
					bDirty = true;

				if ( ImGui::SliderFloat( "Flicker Strength", &light.settings.flFlickerStrength, 0.0f, 1.0f, "%.3f" ) )
					bDirty = true;
			}

			ImGui::Separator();
			ImGui::TextUnformatted( "Shadow" );

			if ( ImGui::SliderFloat( "Shadow Intensity", &light.settings.flShadowIntensity, 0.0f, 1.0f, "%.3f" ) )
				bDirty = true;

			if ( ImGui::DragFloat( "Shadow Bias", &light.settings.flShadowBias, 0.00005f, 0.0f, 0.01f, "%.5f" ) )
				bDirty = true;
		}

		if ( bDirty )
			ApplyLightToGlowObject( light );

		ImGui::PopID();
	}

	if ( iToRemove >= 0 )
	{
		if ( s_iSelectedLight == iToRemove )
			s_iSelectedLight = -1;
		else if ( s_iSelectedLight > iToRemove )
			s_iSelectedLight--;

		RemoveLight( iToRemove );
	}

	ImGui::End();

	// -----------------------------------------------------------------------
	// ImGuizmo -- manipulate the selected light in world space
	// -----------------------------------------------------------------------
	if ( s_iSelectedLight >= 0 && s_iSelectedLight < s_iNumLights )
	{
		TMatrix44 oView, oProj;
		if ( BuildGizmoMatrices( oView, oProj ) )
		{
			EditorLight& light = s_aLights[ s_iSelectedLight ];

			// Build the light's current world transform as a 4x4 float matrix.
			TMatrix44 oModel = BuildLightTransform( light.vPosition, light.flAzimuth, light.flElevation );

			const float* pView  = reinterpret_cast<const float*>( &oView );
			const float* pProj  = reinterpret_cast<const float*>( &oProj );
			float*       pModel = reinterpret_cast<float*>( &oModel );

			// Draw the gizmo into the foreground layer so it's always visible.
			ImGuizmo::SetDrawlist( ImGui::GetForegroundDrawList() );

			if ( ImGuizmo::Manipulate( pView, pProj, s_eGizmoOp, ImGuizmo::WORLD, pModel ) )
			{
				// Extract updated position from the matrix translation row.
				light.vPosition[ 0 ] = oModel.m_f41;
				light.vPosition[ 1 ] = oModel.m_f42;
				light.vPosition[ 2 ] = oModel.m_f43;

				if ( s_eGizmoOp == ImGuizmo::ROTATE )
				{
					// Re-derive azimuth / elevation from the (possibly rotated) forward vector.
					const TVector4& vFwd  = oModel.AsBasisVector4( BASISVECTOR_FORWARD );
					const TFLOAT    fFwdY = TMath::Max( -1.0f, TMath::Min( 1.0f, vFwd.y ) );
					light.flElevation = TMath::ASin( fFwdY ) * ( 180.0f / TMath::PI );
					light.flAzimuth   = TMath::ATan2( vFwd.x, vFwd.z ) * ( 180.0f / TMath::PI );
				}

				ApplyLightToGlowObject( light );
			}
		}
	}
}

} // namespace editor
