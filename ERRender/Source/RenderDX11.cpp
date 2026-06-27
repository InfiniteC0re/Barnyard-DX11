#include "pch.h"
#include "RenderDX11.h"
#include "RenderAdapterDX11.h"
#include "RenderContentDX11.h"
#include "RenderDX11Utils.h"
#include "UI/FontRenderer.h"
#include "Generated/ShaderCombos.h"

#include <dxgi1_5.h>

#include <BYardSDK/THookedRenderD3DInterface.h>
#include <BYardSDK/SDKHooks.h>

#include <AHooks.h>

#include <Toshi/TTask.h>
#include <BYardSDK/ARenderer.h>

#include <Render/TShader.h>
#include <Render/TViewport.h>

#include <Platform/DX8/TModel_DX8.h>
#include <Platform/DX8/TTextureFactoryHAL_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster
{

TDEFINE_CLASS( RenderDX11 );
RenderDX11* g_pRender = TNULL;

static TMemory::MemBlock* s_pRenderHeap = TNULL;

// Flip-model swapchains require at least 2 buffers. Keeping it low avoids
// adding presentation latency on top of the game's uncapped framerate.
static constexpr TUINT SWAPCHAIN_BUFFER_COUNT = 2;

// When enabled (and supported by the GPU/driver), the swapchain presents with
// tearing, bypassing DWM's vblank synchronization.  When disabled, flip-model
// presentation is capped to the refresh rate (smooth, no tearing).
// Only has an effect while presenting with a sync interval of 0 (VSync off).
#define RENDER_ALLOW_TEARING 1

RenderDX11::RenderDX11()
    : m_DepthState( { 0 }, 0 )
{
	THookedRenderD3DInterface::SetSingleton( (TRenderD3DInterface*)this );

	// Legacy states (TRenderD3DInterface)
	m_fPixelAspectRatio                  = 1.0f;
	m_AcceleratorTable                   = TNULL;
	m_pAdapterDevice                     = TNULL;
	m_oDisplayParams.uiWidth             = 640; // Default width
	m_oDisplayParams.uiHeight            = 480; // Default height
	m_oDisplayParams.uiColourDepth       = 32;  // Default color depth
	m_oDisplayParams.eDepthStencilFormat = 0;
	m_oDisplayParams.bWindowed           = TTRUE;
	m_fBrightness                        = 0.5f; // Default brightness
	m_fSaturate                          = 0.5f; // Default saturation
	m_bExited                            = TFALSE;
	m_bCheckedCapableColourCorrection    = TFALSE;
	m_bCapableColourCorrection           = TFALSE;
	m_bFailed                            = TFALSE;
	m_Unk1                               = TNULL;
	m_Unk2                               = TNULL;
	m_fContrast                          = 0.583012f; // Default contrast
	m_fGamma                             = 0.420849f; // Default gamma
	m_bChangedColourSettings             = TTRUE;
	m_bEnableColourCorrection            = TTRUE;

	// Modern states (TRenderDX11)
	m_aClearColor[ 0 ] = 0.0f;
	m_aClearColor[ 1 ] = 0.0f;
	m_aClearColor[ 2 ] = 0.0f;
	m_aClearColor[ 3 ] = 1.0f;

	// Rasterizes state
	m_RasterizerState.Flags.Raw            = 0;
	m_RasterizerState.DepthBias            = 0;
	m_RasterizerState.SlopeScaledDepthBias = 0.0f;

	m_PreviousRasterizerId.Flags.Raw            = 0;
	m_PreviousRasterizerId.DepthBias            = 0;
	m_PreviousRasterizerId.SlopeScaledDepthBias = 0.0f;

	// Buffers
	m_pVertexConstantBuffer         = TNULL;
	m_IsVertexConstantBufferUpdated = TFALSE;
	m_VertexBufferNewSize           = 0;
	m_VertexBufferCurSize           = 0;
	TUtil::MemClear( m_PixelBuffers, sizeof( m_PixelBuffers ) );

	m_pPixelConstantBuffer     = TNULL;
	m_IsPixelConstantBufferSet = TFALSE;
	TUtil::MemClear( m_VertexBuffers, sizeof( m_VertexBuffers ) );

	m_MainVertexBuffer              = TNULL;
	m_iImmediateVertexCurrentOffset = 0;
	m_MainIndexBuffer               = TNULL;
	m_iImmediateIndexCurrentOffset  = 0;
	m_pShadowConstantBuffer         = TNULL;
	m_pScreenRectangleVertexShader  = TNULL;
	m_pRedTintPixelShader           = TNULL;
	m_pScreenRectangleInputLayout   = TNULL;

	// Clear sampler states array
	TUtil::MemClear( m_aSamplerStates, sizeof( m_aSamplerStates ) );

	// Default blend factor
	m_aCurrentBlendFactor[ 0 ] = 0.0f;
	m_aCurrentBlendFactor[ 1 ] = 0.0f;
	m_aCurrentBlendFactor[ 2 ] = 0.0f;
	m_aCurrentBlendFactor[ 3 ] = 0.0f;

	m_PreviousBlendFactor[ 0 ] = 0.0f;
	m_PreviousBlendFactor[ 1 ] = 0.0f;
	m_PreviousBlendFactor[ 2 ] = 0.0f;
	m_PreviousBlendFactor[ 3 ] = 0.0f;

	m_pCurrentRenderTargetView = TNULL;
	m_pCurrentDepthStencilView = TNULL;

	m_pCurrentVertexShader = TNULL;
	m_pCurrentPixelShader  = TNULL;
	m_pCurrentInputLayout  = TNULL;
	m_pCurrentPipelineVertexShaderSlot = TNULL;
	m_pCurrentPipelinePixelShaderSlot  = TNULL;

	for ( auto& pResource : m_apShaderResourceViewsPS )
		pResource = TNULL;

	for ( auto& pResource : m_apShaderResourceViewsVS )
		pResource = TNULL;

	g_pRender = this;
}

RenderDX11::~RenderDX11()
{
	// HACK: figure out reason of the crash that happens at quit
	TerminateProcess( GetCurrentDisplayParams(), 0 );
	g_pRender = TNULL;
}

TBOOL RenderDX11::CreateDisplay( const DISPLAYPARAMS& a_rParams )
{
	if ( !TRenderInterface::CreateDisplay() )
	{
		OnInitializationFailureDisplay();
		return TFALSE;
	}

	if ( a_rParams.uiWidth == 0 || a_rParams.uiHeight == 0 )
	{
		TINT iDisplayIndex = SDL_GetWindowDisplayIndex( m_Window.GetSDLHandle() );

		SDL_DisplayMode oSDLMode;
		SDL_GetCurrentDisplayMode( iDisplayIndex, &oSDLMode );

		// Get rid of the const, because we must override the dimensions now
		DISPLAYPARAMS* pDisplayParams = (DISPLAYPARAMS*)&a_rParams;
		pDisplayParams->uiWidth       = 800;
		pDisplayParams->uiHeight      = 600;
	}

	// Find appropriate device for the display parameters
	m_pAdapterDevice = TSTATICCAST( RenderAdapterDX11::Mode::Device, FindDevice( a_rParams ) );
	m_oDisplayParams = a_rParams;

	if ( m_pDevice )
	{
		auto pDisplayParams = GetCurrentDisplayParams();

		pDisplayParams->bWindowed = TTRUE; // for now force it to be windowed

		// Get device information
		auto pDevice        = TSTATICCAST( RenderAdapterDX11::Mode::Device, GetCurrentDevice() );
		auto pMode          = TSTATICCAST( RenderAdapterDX11::Mode, pDevice->GetMode() );
		auto pAdapter       = TSTATICCAST( RenderAdapterDX11, pMode->GetAdapter() );
		auto uiAdapterIndex = pAdapter->GetAdapterIndex();

		// Clamp the desired MSAA sample count to what this device actually supports
		// for the render-target and depth formats before we create any MSAA resources.
		m_uiMSAASampleCount = GetSupportedMSAASampleCount( MSAA_SAMPLE_COUNT );

		// Create swapchain
		IDXGIDevice* dxgiDevice = TNULL;
		DX11_API_VALIDATE_EXIT( m_pDevice->QueryInterface( __uuidof( IDXGIDevice ), (void**)&dxgiDevice ) );

		IDXGIAdapter* dxgiAdapter = TNULL;
		DX11_API_VALIDATE_EXIT( dxgiDevice->GetAdapter( &dxgiAdapter ) );

		IDXGIFactory2* dxgiFactory = TNULL;
		DX11_API_VALIDATE_EXIT( dxgiAdapter->GetParent( __uuidof( IDXGIFactory2 ), (void**)&dxgiFactory ) );

#if RENDER_ALLOW_TEARING
		{
			IDXGIFactory5* dxgiFactory5 = TNULL;
			if ( SUCCEEDED( dxgiFactory->QueryInterface( __uuidof( IDXGIFactory5 ), (void**)&dxgiFactory5 ) ) )
			{
				BOOL bTearingSupported = FALSE;
				if ( SUCCEEDED( dxgiFactory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &bTearingSupported, sizeof( bTearingSupported ) ) ) )
					m_bAllowTearing = bTearingSupported != FALSE;

				dxgiFactory5->Release();
			}
		}
#endif

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width              = a_rParams.uiWidth;
		swapChainDesc.Height             = a_rParams.uiHeight;
		swapChainDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo             = FALSE;
		// Flip-model backbuffers cannot be MSAA; we render to the MSAA
		// m_pRenderTargetTexture and resolve into the backbuffer in EndScene.
		swapChainDesc.SampleDesc.Count   = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount        = SWAPCHAIN_BUFFER_COUNT;
		swapChainDesc.Scaling            = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags              = m_bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		// Windowed swapchain (the game forces windowed and manages fullscreen
		// itself via the SDL window), so no fullscreen descriptor is needed.
		IDXGISwapChain1* pSwapChain1 = TNULL;
		DX11_API_VALIDATE_EXIT( dxgiFactory->CreateSwapChainForHwnd( m_pDevice, m_Window.GetWin32Handle(), &swapChainDesc, TNULL, TNULL, &pSwapChain1 ) );

		m_pSwapChain = pSwapChain1;
		m_pSwapChain->GetDesc( &m_oSwapChainDesc );

		dxgiFactory->Release();
		dxgiAdapter->Release();
		dxgiDevice->Release();

		// Create the swapchain-size-dependent resources (colour/glow/G-buffer/depth +
		// the back-buffer reference). Split out so a runtime resolution/MSAA change
		// can release and recreate them without re-running the whole CreateDisplay path.
		CreateSwapchainSizedResources();

		// Seed the runtime graphics settings from the values resolved above so the
		// pending/active snapshots match the live device state.
		m_oActiveSettings.uiWidth       = m_oSwapChainDesc.BufferDesc.Width;
		m_oActiveSettings.uiHeight      = m_oSwapChainDesc.BufferDesc.Height;
		m_oActiveSettings.eDisplayMode  = pDisplayParams->bWindowed ? DISPLAY_WINDOWED : DISPLAY_BORDERLESS;
		m_oActiveSettings.bVSync        = ( m_uiSyncInterval != 0 );
		m_oActiveSettings.uiMSAASamples = m_uiMSAASampleCount;
		m_oActiveSettings.eCSMPreset    = m_oCSMManager.GetPreset();
		m_oPendingSettings              = m_oActiveSettings;
		m_uiGraphicsDirty               = GFX_DIRTY_NONE;

		s_pRenderHeap = g_pMemory->CreateMemBlock( HEAPSIZE, "RenderDX11", TNULL, 0 );
		CreateRenderObjects();
		CreateRenderTargets();

		// Set window position and size
		m_Window.SetPosition( SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, a_rParams.uiWidth, a_rParams.uiHeight );

		// Set window mode
		m_Window.SetFullscreen( !pDisplayParams->bWindowed );
		m_Window.Show();

		// Create invalid texture pattern
		TUINT invalidTextureData[ 32 ];
		for ( TINT i = 0; i < 32; i++ )
		{
			invalidTextureData[ i ] = 0xff0fff0f;
		}

		auto pTextureFactory = GetSystemResource<Toshi::TTextureFactoryHAL>( SYSRESOURCE_TEXTUREFACTORY );
		m_pInvalidTexture    = pTextureFactory->CreateTextureFromMemory( invalidTextureData, sizeof( invalidTextureData ), 0x11, 8, 8 );

		// Enable color correction and mark display as created
		EnableColourCorrection( TTRUE );
		m_bDisplayCreated = TTRUE;

		// Initialize HD font renderer
		if ( fontrenderer::IsHDEnabled() )
		{
			{
				// Rekord26
				ID3D11ShaderResourceView* pAtlasSRV = dx11::CreateTexture(
				    1024,
				    1024,
				    DXGI_FORMAT_R8_UNORM,
				    TNULL,
				    D3D11_USAGE_DEFAULT,
				    0,
				    1,
				    dx11::CTF_RENDER_TARGET
				);

				TVALIDPTR( pAtlasSRV );

				ID3D11Resource* pResource = TNULL;
				pAtlasSRV->GetResource( &pResource );

				ID3D11Texture2D* pTextAtlasTexture = TNULL;
				pResource->QueryInterface( __uuidof( ID3D11Texture2D ), (void**)&pTextAtlasTexture );
				pResource->Release();

				IDXGISurface* pBackBufferSurface = TNULL;
				pTextAtlasTexture->QueryInterface( __uuidof( IDXGISurface ), (void**)&pBackBufferSurface );

				m_pFontAtlases[ FONT_REKORD26 ] = new FontAtlas( pAtlasSRV, ".\\Resources\\Fonts\\CCThatsAllFolks.ttf", pTextAtlasTexture, 1024, 1024, 1.1f, 0.6f, 0.85f );
				pTextAtlasTexture->Release();
			}

			{
				// Rekord18
				ID3D11ShaderResourceView* pAtlasSRV = dx11::CreateTexture(
				    1024,
				    1024,
				    DXGI_FORMAT_R8_UNORM,
				    TNULL,
				    D3D11_USAGE_DEFAULT,
				    0,
				    1,
				    dx11::CTF_RENDER_TARGET
				);

				TVALIDPTR( pAtlasSRV );

				ID3D11Resource* pResource = TNULL;
				pAtlasSRV->GetResource( &pResource );

				ID3D11Texture2D* pTextAtlasTexture = TNULL;
				pResource->QueryInterface( __uuidof( ID3D11Texture2D ), (void**)&pTextAtlasTexture );
				pResource->Release();

				IDXGISurface* pBackBufferSurface = TNULL;
				pTextAtlasTexture->QueryInterface( __uuidof( IDXGISurface ), (void**)&pBackBufferSurface );

				m_pFontAtlases[ FONT_REKORD18 ] = new FontAtlas( pAtlasSRV, ".\\Resources\\Fonts\\AmmanSansPro-Bold.ttf", pTextAtlasTexture, 1024, 1024, 0.8f, 0.5f, 0.8f, 0.0f, 0.0f, 6.0f );
				pTextAtlasTexture->Release();
			}
		}

		return TTRUE;
	}

	OnInitializationFailureDisplay();
	return TFALSE;
}

TBOOL RenderDX11::DestroyDisplay()
{
	throw std::logic_error( "The method or operation is not implemented." );
}

TBOOL RenderDX11::Update( TFLOAT a_fDeltaTime )
{
	// Apply any pending runtime graphics changes here. Called between frames, outside any
	// BeginScene/EndScene pair, so it's safe to release and recreate device resources.
	ApplyGraphicsSettings();

	FlushDyingResources();
	m_Window.Update();

	return !m_bExited;
}

TBOOL RenderDX11::BeginScene()
{
	static constexpr TFLOAT CLEAR_COLOR[] = { 0.0f, 0.0f, 0.0f, 1.0f };

	if ( BaseClass::BeginScene() )
	{
		ClearStateCache();
		m_bInScene = TTRUE;

		// Reset ring-buffer offsets each frame.  The first Map this frame will
		// use DISCARD (offset == 0), subsequent ones within the same frame will
		// use NO_OVERWRITE and just append into the same allocation.
		m_iImmediateVertexCurrentOffset = 0;
		m_iImmediateIndexCurrentOffset  = 0;

		SetRenderTargetView( m_pRenderTargetView, m_pDepthStencilView );
		ClearCurrentRenderTarget( CLEAR_COLOR );

		D3D11_VIEWPORT viewport;
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.Width    = TFLOAT( m_oSwapChainDesc.BufferDesc.Width );
		viewport.Height   = TFLOAT( m_oSwapChainDesc.BufferDesc.Height );

		m_pDeviceContext->RSSetViewports( 1, &viewport );

		return TTRUE;
	}

	return TFALSE;
}

TBOOL RenderDX11::EndScene()
{
	TPROFILER_SCOPE();

	{
		TracyD3D11Zone( m_pTracyD3D11Ctx, "Resolve + Postprocess" );

		// Resolve/copy HDR main into a non-MSAA pad, then saturate to the LDR back buffer.
		if ( m_uiMSAASampleCount > 1 )
			m_pDeviceContext->ResolveSubresource( m_pPresentResolveTexture, 0, m_pRenderTargetTexture, 0, DXGI_FORMAT_R11G11B10_FLOAT );
		else
			m_pDeviceContext->CopyResource( m_pPresentResolveTexture, m_pRenderTargetTexture );

		SetRenderTargetView( m_pSwapChainBackBufferRTV, TNULL );
		SetCullMode( D3D11_CULL_NONE );
		SetDepthEnabled( TFALSE );
		SetBlendEnabled( TFALSE );
		PSSetShaderResource( 0, m_pPresentResolveSRV );
		PSSetSamplerState( 0, SAMPLER_POINT_CLAMP );
		DrawScreenRectangle(
		    remaster::shadercombos::GetPostprocessPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::Postprocess_NoCombos )
		);
		PSSetShaderResource( 0, TNULL );
		ClearStateCache();
	}

	// DXGI_PRESENT_ALLOW_TEARING is only valid with a sync interval of 0 and a
	// swapchain created with DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING.
	const TBOOL bTearing = ( m_uiSyncInterval == 0 ) && m_bAllowTearing;
	m_pSwapChain->Present( m_uiSyncInterval, bTearing ? DXGI_PRESENT_ALLOW_TEARING : 0 );
	m_bInScene = TFALSE;

	// One disjoint-query span per frame: read back the timestamps queued this frame.
	// Must be called exactly once per frame on the immediate-context thread.
	TracyD3D11Collect( m_pTracyD3D11Ctx );

	return TTRUE;
}

TRenderAdapter::Mode::Device* RenderDX11::GetCurrentDevice()
{
	return m_pAdapterDevice;
}

TRenderInterface::DISPLAYPARAMS* RenderDX11::GetCurrentDisplayParams()
{
	return &m_oDisplayParams;
}

void RenderDX11::FlushOrderTables()
{
	TASSERT( TTRUE == IsInScene() );

	TPROFILER_SCOPE();
	TracyD3D11Zone( m_pTracyD3D11Ctx, "Order Tables" );

	for ( auto it = m_OrderTables.Begin(); it != m_OrderTables.End(); it++ )
	{
		it->Flush();
	}
}

TBOOL RenderDX11::Supports32BitTextures()
{
	return TTRUE;
}

TRenderContext* RenderDX11::CreateRenderContext()
{
	return new RenderContextD3D11( this );
}

TRenderCapture* RenderDX11::CreateCapture()
{
	throw std::logic_error( "The method or operation is not implemented." );
}

void RenderDX11::DestroyCapture( TRenderCapture* a_pRenderCapture )
{
	throw std::logic_error( "The method or operation is not implemented." );
}

void* RenderDX11::CreateUnknown( const TCHAR* a_szName, TINT a_iUnk1, TINT a_iUnk2, TINT a_iUnk3 )
{
	return TNULL;
}

TModel* RenderDX11::CreateModelTMD( TTMD* a_pTMD, TBOOL a_bLoad )
{
	TASSERT( FALSE );
	return TNULL;
}

TModel* RenderDX11::CreateModelTMDFile( const TCHAR* a_szFilePath, TBOOL a_bLoad )
{
	TPROFILER_SCOPE();

	auto pModel = new TModelHAL();

	if ( pModel )
	{
		if ( !pModel->Create( a_szFilePath, a_bLoad ) )
		{
			pModel->Delete();
			return TNULL;
		}
	}

	return pModel;
}

TModel* RenderDX11::CreateModelTRB( const TCHAR* a_szFilePath, TBOOL a_bLoad, TTRB* a_pAssetTRB, TUINT8 a_ui8FileNameLen )
{
	TPROFILER_SCOPE();

	auto pModel = new TModelHAL();

	if ( pModel )
	{
		if ( !pModel->Create( a_szFilePath, a_bLoad, a_pAssetTRB, a_ui8FileNameLen ) )
		{
			pModel->Delete();
			return TNULL;
		}
	}

	return pModel;
}

TDebugText* RenderDX11::CreateDebugText()
{
	throw std::logic_error( "The method or operation is not implemented." );
}

void RenderDX11::DestroyDebugText()
{
	throw std::logic_error( "The method or operation is not implemented." );
}

// Resizes the game's render viewports to the new backbuffer size. Without this the
// scene keeps rendering into the old viewport rectangle (ARenderer::CreateMainViewport
// sizes these once from the startup display params), so the new resolution would only
// be used by the post-process targets. SetWidth/SetHeight push straight into each
// viewport's render-context params (see TViewport_BeginSKU).
static void UpdateGameViewports( TUINT a_uiWidth, TUINT a_uiHeight )
{
	ARenderer* pRenderer = ARenderer::GetSingleton();
	if ( !pRenderer )
		return;

	const TFLOAT fWidth  = TFLOAT( a_uiWidth );
	const TFLOAT fHeight = TFLOAT( a_uiHeight );

	Toshi::TViewport* apViewports[] = { pRenderer->m_pViewport, pRenderer->m_pHALViewport1, pRenderer->m_pHALViewport2 };
	for ( Toshi::TViewport* pViewport : apViewports )
	{
		if ( !pViewport )
			continue;

		pViewport->SetWidth( fWidth );
		pViewport->SetHeight( fHeight );
	}
}

TBOOL RenderDX11::RecreateDisplay( const DISPLAYPARAMS& a_rDisplayParams )
{
	if ( !IsDisplayCreated() || !m_pSwapChain )
		return TFALSE;

	// Make sure the GPU is finished with the resources we are about to release.
	WaitForEndOfRender();

	// Unbind everything on the device so the resources we release (and the swapchain
	// backbuffer) have no outstanding references for ResizeBuffers.
	SetSecondaryRenderTargetView( TNULL );
	m_pDeviceContext->ClearState();

	ReleaseSwapchainSizedResources();
	ReleaseRenderTargets();

	// Only the swapchain backbuffer needs a true resize; MSAA-only changes keep the
	// (always 1-sample, flip-model) backbuffer and just rebuild the offscreen targets.
	const TBOOL bSizeChanged = ( a_rDisplayParams.uiWidth != m_oSwapChainDesc.BufferDesc.Width ) ||
	                           ( a_rDisplayParams.uiHeight != m_oSwapChainDesc.BufferDesc.Height );
	if ( bSizeChanged )
	{
		const UINT uiFlags = m_bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		DX11_API_VALIDATE( m_pSwapChain->ResizeBuffers(
		    SWAPCHAIN_BUFFER_COUNT,
		    a_rDisplayParams.uiWidth,
		    a_rDisplayParams.uiHeight,
		    DXGI_FORMAT_R8G8B8A8_UNORM,
		    uiFlags
		) );
	}

	m_pSwapChain->GetDesc( &m_oSwapChainDesc );

	CreateSwapchainSizedResources();
	CreateRenderTargets();

	m_oDisplayParams           = a_rDisplayParams;
	m_oDisplayParams.uiWidth   = m_oSwapChainDesc.BufferDesc.Width;
	m_oDisplayParams.uiHeight  = m_oSwapChainDesc.BufferDesc.Height;

	// Apply the window mode/size to the SDL window.
	m_Window.SetFullscreen( !a_rDisplayParams.bWindowed );
	if ( a_rDisplayParams.bWindowed )
		m_Window.SetPosition( SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_oSwapChainDesc.BufferDesc.Width, m_oSwapChainDesc.BufferDesc.Height );

	// ClearState() unbound everything on the device; fully reset our binding cache so the
	// next frame rebinds it all (a partial reset would leave e.g. the skin bone cbuffer
	// cached-but-unbound, making animated meshes vanish).
	InvalidateStateCache();

	// Resize the game's viewports so the scene renders at the new resolution.
	UpdateGameViewports( m_oSwapChainDesc.BufferDesc.Width, m_oSwapChainDesc.BufferDesc.Height );

	// Re-apply the resolution-dependent widescreen patches (aspect-ratio FOV + AGUI2
	// canvas), which were otherwise only computed once at startup.
	ApplyResolutionDependentPatches( m_oSwapChainDesc.BufferDesc.Width, m_oSwapChainDesc.BufferDesc.Height );

	return TTRUE;
}

//-----------------------------------------------------------------------------
// Runtime graphics settings. Request methods only record the desired value and a dirty
// bit; the work happens in ApplyGraphicsSettings() between frames.
//-----------------------------------------------------------------------------
void RenderDX11::RequestResolution( TUINT a_uiWidth, TUINT a_uiHeight )
{
	if ( a_uiWidth == 0 || a_uiHeight == 0 )
		return;

	if ( m_oPendingSettings.uiWidth == a_uiWidth && m_oPendingSettings.uiHeight == a_uiHeight )
		return;

	m_oPendingSettings.uiWidth  = a_uiWidth;
	m_oPendingSettings.uiHeight = a_uiHeight;
	m_uiGraphicsDirty |= GFX_DIRTY_RESOLUTION;
}

void RenderDX11::RequestDisplayMode( DisplayMode a_eMode )
{
	if ( m_oPendingSettings.eDisplayMode == a_eMode )
		return;

	m_oPendingSettings.eDisplayMode = a_eMode;
	m_uiGraphicsDirty |= GFX_DIRTY_DISPLAYMODE;
}

void RenderDX11::RequestVSync( TBOOL a_bEnabled )
{
	if ( m_oPendingSettings.bVSync == a_bEnabled )
		return;

	m_oPendingSettings.bVSync = a_bEnabled;
	m_uiGraphicsDirty |= GFX_DIRTY_VSYNC;
}

void RenderDX11::RequestMSAA( TUINT a_uiSamples )
{
	if ( m_oPendingSettings.uiMSAASamples == a_uiSamples )
		return;

	m_oPendingSettings.uiMSAASamples = a_uiSamples;
	m_uiGraphicsDirty |= GFX_DIRTY_MSAA;
}

void RenderDX11::RequestCSMPreset( CSMPreset a_ePreset )
{
	if ( m_oPendingSettings.eCSMPreset == a_ePreset )
		return;

	m_oPendingSettings.eCSMPreset = a_ePreset;
	m_uiGraphicsDirty |= GFX_DIRTY_CSM;
}

void RenderDX11::ApplyGraphicsSettings()
{
	if ( m_uiGraphicsDirty == GFX_DIRTY_NONE )
		return;

	// Never reconfigure the device mid-scene; deferred to the next Update().
	if ( IsInScene() )
		return;

	// VSync: cheap, just changes the Present sync interval (consumed in EndScene).
	if ( m_uiGraphicsDirty & GFX_DIRTY_VSYNC )
		m_uiSyncInterval = m_oPendingSettings.bVSync ? 1 : 0;

	// Resolution / MSAA / display-mode: route through RecreateDisplay (the engine's
	// display-change entry point), which rebuilds the swapchain-sized resources and
	// resizes the game viewports. The game's own options menu calls the same path.
	if ( m_uiGraphicsDirty & ( GFX_DIRTY_RESOLUTION | GFX_DIRTY_MSAA | GFX_DIRTY_DISPLAYMODE ) )
	{
		// MSAA isn't part of DISPLAYPARAMS; CreateSwapchainSizedResources reads the member.
		if ( m_uiGraphicsDirty & GFX_DIRTY_MSAA )
			m_uiMSAASampleCount = GetSupportedMSAASampleCount( m_oPendingSettings.uiMSAASamples );

		const TBOOL bWindowed = ( m_oPendingSettings.eDisplayMode == DISPLAY_WINDOWED );

		// Borderless/fullscreen tracks the current desktop resolution.
		if ( !bWindowed )
		{
			const TINT      iDisplayIndex = SDL_GetWindowDisplayIndex( m_Window.GetSDLHandle() );
			SDL_DisplayMode oSDLMode;
			if ( SDL_GetCurrentDisplayMode( iDisplayIndex, &oSDLMode ) == 0 )
			{
				m_oPendingSettings.uiWidth  = TUINT( oSDLMode.w );
				m_oPendingSettings.uiHeight = TUINT( oSDLMode.h );
			}
		}

		DISPLAYPARAMS oParams     = m_oDisplayParams;
		oParams.uiWidth           = m_oPendingSettings.uiWidth;
		oParams.uiHeight          = m_oPendingSettings.uiHeight;
		oParams.bWindowed         = bWindowed;

		RecreateDisplay( oParams );
	}

	// CSM shadow atlas resize.
	if ( m_uiGraphicsDirty & GFX_DIRTY_CSM )
		m_oCSMManager.ApplyResolution( m_oPendingSettings.eCSMPreset );

	m_oActiveSettings = m_oPendingSettings;
	m_uiGraphicsDirty = GFX_DIRTY_NONE;
}

void RenderDX11::SetContrast( TFLOAT a_fConstrast )
{
	m_fContrast = a_fConstrast;
}

void RenderDX11::SetBrightness( TFLOAT a_fBrightness )
{
	m_fBrightness = a_fBrightness;
}

void RenderDX11::SetGamma( TFLOAT a_fGamma )
{
	m_fGamma = a_fGamma;
}

void RenderDX11::SetSaturate( TFLOAT a_fSaturate )
{
	m_fSaturate = a_fSaturate;
}

TFLOAT RenderDX11::GetContrast() const
{
	return m_fContrast;
}

TFLOAT RenderDX11::GetBrightness() const
{
	return m_fBrightness;
}

TFLOAT RenderDX11::GetGamma() const
{
	return m_fGamma;
}

TFLOAT RenderDX11::GetSaturate() const
{
	return m_fSaturate;
}

void RenderDX11::UpdateColourSettings()
{
}

TBOOL RenderDX11::IsCapableColourCorrection()
{
	return TTRUE;
}

void RenderDX11::EnableColourCorrection( TBOOL a_bEnable )
{
}

void RenderDX11::ForceEnableColourCorrection( TBOOL a_bEnable )
{
}

TBOOL RenderDX11::IsColourCorrection()
{
	return TTRUE;
}

TBOOL RenderDX11::Create( const TCHAR* a_pchWindowTitle )
{
	if ( TBOOL bRenderInterfaceCreated = TRenderInterface::Create() )
	{
		TUINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED
#if defined( TOSHI_DEBUG )
		    | D3D11_CREATE_DEVICE_DEBUG
#endif
		    ;

		BuildAdapterDatabase();
		DX11_API_VALIDATE_EXIT( D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0, D3D11_SDK_VERSION, &m_pDevice, &m_eFeatureLevel, &m_pDeviceContext ) );

		if ( FAILED( m_pDeviceContext->QueryInterface( __uuidof( ID3D11DeviceContext1 ), (void**)&m_pDeviceContext1 ) ) )
		{
			TINFO( "ID3D11DeviceContext1 unavailable" );
			m_pDeviceContext1 = TNULL;
		}

		// GPU timestamp profiling on the immediate context (no-op unless --profiler=perf).
		// Must run after the device + immediate context exist; calibrates CPU<->GPU clocks.
		m_pTracyD3D11Ctx = TracyD3D11Context( m_pDevice, m_pDeviceContext );
		TracyD3D11ContextName( m_pTracyD3D11Ctx, "DX11 Immediate", sizeof( "DX11 Immediate" ) - 1 );

		return m_pDevice && m_pDeviceContext && m_Window.Create( this, TString8::VarArgs( "%s - DirectX11", a_pchWindowTitle ) );
	}

	return TFALSE;
}

void RenderDX11::CreateSwapchainSizedResources()
{
	// Main render target: HDR float so lighting can go >1.0 for bloom; saturated at present.
	D3D11_TEXTURE2D_DESC mainRTDesc = {};
	mainRTDesc.ArraySize            = 1;
	mainRTDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	mainRTDesc.CPUAccessFlags       = 0;
	// HDR scene buffer. R11G11B10 packs HDR into 32bpp (half of RGBA16F) -- it has no alpha,
	// which is fine here: alpha-to-coverage and SRC_ALPHA blending consume the PS-output alpha
	// at the OM stage, and no pass reads the scene buffer's stored alpha. The reduced mantissa
	// (6/6/5 bits) can band in smooth sky/fog gradients; the final present pass dithers to hide it.
	mainRTDesc.Format               = DXGI_FORMAT_R11G11B10_FLOAT;
	mainRTDesc.Height               = m_oSwapChainDesc.BufferDesc.Height;
	mainRTDesc.Width                = m_oSwapChainDesc.BufferDesc.Width;
	mainRTDesc.MipLevels            = 1;
	mainRTDesc.MiscFlags            = 0;
	mainRTDesc.SampleDesc.Count     = m_uiMSAASampleCount;
	mainRTDesc.SampleDesc.Quality   = 0;
	mainRTDesc.Usage                = D3D11_USAGE_DEFAULT;

	// Glow render target stays LDR to preserve the existing additive-blend look.
	D3D11_TEXTURE2D_DESC glowRTDesc = mainRTDesc;
	glowRTDesc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;

	DX11_API_VALIDATE( m_pDevice->CreateTexture2D( &mainRTDesc, TNULL, &m_pRenderTargetTexture ) );
	DX11_API_VALIDATE( m_pDevice->CreateTexture2D( &glowRTDesc, TNULL, &m_pGlowRenderTargetTexture ) );
	DX11_API_VALIDATE( m_pDevice->CreateRenderTargetView( m_pRenderTargetTexture, TNULL, &m_pRenderTargetView ) );
	DX11_API_VALIDATE( m_pDevice->CreateRenderTargetView( m_pGlowRenderTargetTexture, TNULL, &m_pGlowRenderTargetView ) );

	// Main-pass G-buffer. RGBA8: rg = octahedral normal (8-bit oct is fine for SSR),
	// b = reflectivity, a = packed roughness/fresnel. Quarter the size and resolve cost
	// of the old RGBA16F target.
	D3D11_TEXTURE2D_DESC gbufferDesc = mainRTDesc;
	gbufferDesc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
	DX11_API_VALIDATE( m_pDevice->CreateTexture2D( &gbufferDesc, TNULL, &m_pGBufferTexture ) );
	DX11_API_VALIDATE( m_pDevice->CreateRenderTargetView( m_pGBufferTexture, TNULL, &m_pGBufferRTV ) );

	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension             = mainRTDesc.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels       = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		srvDesc.Format = mainRTDesc.Format;
		DX11_API_VALIDATE( m_pDevice->CreateShaderResourceView( m_pRenderTargetTexture, &srvDesc, &m_pRenderTargetSRV ) );

		srvDesc.Format = glowRTDesc.Format;
		DX11_API_VALIDATE( m_pDevice->CreateShaderResourceView( m_pGlowRenderTargetTexture, &srvDesc, &m_pGlowRenderTargetSRV ) );
	}

	DX11_API_VALIDATE( m_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)&m_pSwapChainBackBuffer ) );
	DX11_API_VALIDATE( m_pDevice->CreateRenderTargetView( m_pSwapChainBackBuffer, TNULL, &m_pSwapChainBackBufferRTV ) );

	// Non-MSAA HDR landing pad for the MSAA resolve in the postprocess pass.
	{
		D3D11_TEXTURE2D_DESC presentResolveDesc = mainRTDesc;
		presentResolveDesc.SampleDesc.Count     = 1;
		presentResolveDesc.SampleDesc.Quality   = 0;
		presentResolveDesc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;
		DX11_API_VALIDATE( m_pDevice->CreateTexture2D( &presentResolveDesc, TNULL, &m_pPresentResolveTexture ) );
		DX11_API_VALIDATE( m_pDevice->CreateShaderResourceView( m_pPresentResolveTexture, TNULL, &m_pPresentResolveSRV ) );
	}

	// Create depth stencil view
	D3D11_TEXTURE2D_DESC depthBufferDesc = {};
	depthBufferDesc.ArraySize            = 1;
	depthBufferDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	depthBufferDesc.CPUAccessFlags       = 0;
	// D32_FLOAT: stencil is never used, and D32 enables faster HiZ compression
	// on most IHVs compared to D24S8.  R32_TYPELESS allows the SRV to read it
	// as R32_FLOAT for the depth-resolve pass.
	depthBufferDesc.Format               = DXGI_FORMAT_R32_TYPELESS;
	depthBufferDesc.Height               = m_oSwapChainDesc.BufferDesc.Height;
	depthBufferDesc.Width                = m_oSwapChainDesc.BufferDesc.Width;
	depthBufferDesc.MipLevels            = 1;
	depthBufferDesc.MiscFlags            = 0;
	depthBufferDesc.SampleDesc.Count     = m_uiMSAASampleCount;
	depthBufferDesc.SampleDesc.Quality   = 0;
	depthBufferDesc.Usage                = D3D11_USAGE_DEFAULT;

	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};

	depthStencilDesc.Format             = DXGI_FORMAT_D32_FLOAT;
	depthStencilDesc.Flags              = 0;
	depthStencilDesc.Texture2D.MipSlice = 0;
	depthStencilDesc.ViewDimension      = depthBufferDesc.SampleDesc.Count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;

	DX11_API_VALIDATE( m_pDevice->CreateTexture2D( &depthBufferDesc, TNULL, &m_pDepthStencilTexture ) );
	DX11_API_VALIDATE( m_pDevice->CreateDepthStencilView( m_pDepthStencilTexture, &depthStencilDesc, &m_pDepthStencilView ) );

	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
		srvDesc.ViewDimension             = depthBufferDesc.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels       = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		DX11_API_VALIDATE( m_pDevice->CreateShaderResourceView( m_pDepthStencilTexture, &srvDesc, &m_pDepthStencilSRV ) );
	}
}

void RenderDX11::ReleaseSwapchainSizedResources()
{
	auto fnRelease = []( auto*& a_rpObject )
	{
		if ( a_rpObject )
		{
			a_rpObject->Release();
			a_rpObject = TNULL;
		}
	};

	fnRelease( m_pRenderTargetSRV );
	fnRelease( m_pRenderTargetView );
	fnRelease( m_pRenderTargetTexture );
	fnRelease( m_pGlowRenderTargetSRV );
	fnRelease( m_pGlowRenderTargetView );
	fnRelease( m_pGlowRenderTargetTexture );
	fnRelease( m_pGBufferRTV );
	fnRelease( m_pGBufferTexture );
	fnRelease( m_pDepthStencilSRV );
	fnRelease( m_pDepthStencilView );
	fnRelease( m_pDepthStencilTexture );
	fnRelease( m_pPresentResolveSRV );
	fnRelease( m_pPresentResolveTexture );
	fnRelease( m_pSwapChainBackBufferRTV );
	fnRelease( m_pSwapChainBackBuffer );
}

void RenderDX11::CreateRenderObjects()
{
	const TBOOL bShaderCombosCompiled = shadercombos::CompileAllShaderCombos();
	TASSERT( bShaderCombosCompiled );

	// Sample states
	m_aSamplerStates[ SAMPLER_POINT_CLAMP ]           = CreateSamplerState( D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_LINEAR_CLAMP ]          = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ SAMPLER_POINT_WRAP ]            = CreateSamplerState( D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_LINEAR_WRAP ]           = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ SAMPLER_LINEAR_MIRROR ]         = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_MIRROR, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ SAMPLER_BILINEAR_CLAMP ]        = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_BILINEAR_WRAP ]         = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_BILINEAR_WRAP_BIAS ]    = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, -1.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_ANISO_CLAMP ]           = CreateSamplerState( D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_POINT_WRAPU_CLAMPV ]    = CreateSamplerState( D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_LINEAR_WRAPU_CLAMPV ]   = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ SAMPLER_BILINEAR_WRAPU_CLAMPV ] = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_POINT_CLAMPU_WRAPV ]    = CreateSamplerState( D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_LINEAR_CLAMPU_WRAPV ]   = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ SAMPLER_BILINEAR_CLAMPU_WRAPV ] = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ SAMPLER_BILINEAR_MIRROR ]       = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_MIRROR, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );

	// Vertex buffers
	for ( size_t i = 0; i < NUMBUFFERS; i++ )
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth           = VERTEX_CONSTANT_BUFFER_SIZE;
		bufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags           = 0;
		bufferDesc.StructureByteStride = 0;

		DX11_API_VALIDATE( m_pDevice->CreateBuffer( &bufferDesc, NULL, &m_VertexBuffers[ i ] ) );
	}

	m_pVertexConstantBuffer         = TMemalign( 16, VERTEX_CONSTANT_BUFFER_SIZE, s_pRenderHeap );
	m_IsVertexConstantBufferUpdated = TFALSE;
	m_VertexBufferIndex             = 0;

	// Pixel buffers
	for ( size_t i = 0; i < NUMBUFFERS; i++ )
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth           = PIXEL_CONSTANT_BUFFER_SIZE;
		bufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags           = 0;
		bufferDesc.StructureByteStride = 0;

		DX11_API_VALIDATE( m_pDevice->CreateBuffer( &bufferDesc, NULL, &m_PixelBuffers[ i ] ) );
	}

	m_pPixelConstantBuffer     = TMalloc( PIXEL_CONSTANT_BUFFER_SIZE, s_pRenderHeap );
	m_IsPixelConstantBufferSet = TFALSE;

	// Shadow constant buffer
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth           = SHADOW_CONSTANT_BUFFER_SIZE;
		bufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags           = 0;
		bufferDesc.StructureByteStride = 0;

		DX11_API_VALIDATE( m_pDevice->CreateBuffer( &bufferDesc, NULL, &m_pShadowConstantBuffer ) );
	}

	// Depth only pass constant buffer
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth           = 128;
		bufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags           = 0;
		bufferDesc.StructureByteStride = 0;

		DX11_API_VALIDATE( m_pDevice->CreateBuffer( &bufferDesc, NULL, &m_pDepthPassConstantBuffer ) );
	}

	m_PixelBufferIndex = 0;

	// Main vertex buffer
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth           = IMMEDIATE_VERTEX_BUFFER_SIZE;
		bufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags           = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags           = 0;
		bufferDesc.StructureByteStride = 0;

		DX11_API_VALIDATE( m_pDevice->CreateBuffer( &bufferDesc, NULL, &m_MainVertexBuffer ) );
		m_iImmediateVertexCurrentOffset = 0;
	}

	// Main index buffer
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth           = IMMEDIATE_INDEX_BUFFER_SIZE;
		bufferDesc.Usage               = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags           = D3D11_BIND_INDEX_BUFFER;
		bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags           = 0;
		bufferDesc.StructureByteStride = 0;

		DX11_API_VALIDATE( m_pDevice->CreateBuffer( &bufferDesc, NULL, &m_MainIndexBuffer ) );
		m_iImmediateIndexCurrentOffset = 0;
	}

	// Screen space rectangle shader
	{
		if ( bShaderCombosCompiled )
		{
			const dx11::ShaderCombo& rScreenSpaceVSCombo = shadercombos::GetScreenSpaceVertexShaderCombo_vs_main();
			ID3DBlob*               pVSBlob             = rScreenSpaceVSCombo.GetBlob( 0 );
			TVALIDPTR( pVSBlob );

			if ( pVSBlob )
			{
				if ( !shadercombos::CreateScreenSpaceVertexShader_vs_main( &m_pScreenRectangleVertexShader ) )
					TASSERT( TFALSE );

				if ( !shadercombos::CreateScreenSpacePixelShader_ps_red_tint( &m_pRedTintPixelShader ) )
					TASSERT( TFALSE );

				D3D11_INPUT_ELEMENT_DESC aInputElements[] = {
					{ .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32_FLOAT, .InputSlot = 0, .AlignedByteOffset = 0, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
					{ .SemanticName = "TEXCOORD", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32_FLOAT, .InputSlot = 0, .AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT, .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
				};

				DX11_API_VALIDATE(
				    m_pDevice->CreateInputLayout(
				        aInputElements,
				        TARRAYSIZE( aInputElements ),
				        pVSBlob->GetBufferPointer(),
				        pVSBlob->GetBufferSize(),
				        &m_pScreenRectangleInputLayout
				    )
				);
			}
		}
	}

	// Depth state
	m_DepthState.first.Parts.bDepthEnable                = TRUE;
	m_DepthState.first.Parts.DepthWriteMask              = D3D11_DEPTH_WRITE_MASK_ALL;
	m_DepthState.first.Parts.DepthFunc                   = D3D11_COMPARISON_LESS;
	m_DepthState.first.Parts.bStencilEnable              = FALSE;
	m_DepthState.first.Parts.StencilReadMask             = 0b11111111;
	m_DepthState.first.Parts.StencilWriteMask            = 0b11111111;
	m_DepthState.first.Parts.FrontFaceStencilFailOp      = D3D11_STENCIL_OP_KEEP;
	m_DepthState.first.Parts.FrontFaceStencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	m_DepthState.first.Parts.FrontStencilPassOp          = D3D11_STENCIL_OP_KEEP;
	m_DepthState.first.Parts.FrontStencilFunc            = D3D11_COMPARISON_ALWAYS;
	m_DepthState.first.Parts.BackFaceStencilFailOp       = D3D11_STENCIL_OP_KEEP;
	m_DepthState.first.Parts.BackFaceStencilDepthFailOp  = D3D11_STENCIL_OP_KEEP;
	m_DepthState.first.Parts.BackStencilPassOp           = D3D11_STENCIL_OP_KEEP;
	m_DepthState.first.Parts.BackStencilFunc             = D3D11_COMPARISON_ALWAYS;

	// Blend state
	m_BlendState.Parts.BlendOp               = D3D11_BLEND_OP_ADD;
	m_BlendState.Parts.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
	m_BlendState.Parts.SrcBlendAlpha         = D3D11_BLEND_ONE;
	m_BlendState.Parts.DestBlendAlpha        = D3D11_BLEND_ZERO;
	m_BlendState.Parts.RenderTargetWriteMask = 0b1111;
	m_BlendState.Parts.SrcBlend              = D3D11_BLEND_ONE;
	m_BlendState.Parts.DestBlend             = D3D11_BLEND_ZERO;
	m_BlendState.Parts.bAlphaToCoverage      = FALSE;

	// Rasterizer state
	m_RasterizerState.Flags.Parts.FillMode               = D3D11_FILL_SOLID;
	m_RasterizerState.Flags.Parts.CullMode               = D3D11_CULL_BACK;
	m_RasterizerState.Flags.Parts.bFrontCounterClockwise = FALSE;
	m_RasterizerState.Flags.Parts.bDepthClipEnable       = TRUE;
	m_RasterizerState.Flags.Parts.bScissorEnable         = FALSE;
	m_RasterizerState.Flags.Parts.bMultisampleEnable     = FALSE;
	m_RasterizerState.DepthBias                          = 0;
	m_RasterizerState.SlopeScaledDepthBias               = 0.0f;

	// Other states
	m_eCurrentTopology     = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	m_pCurrentVertexBuffer = TNULL;

	m_oCSMManager.Create();
}

TUINT RenderDX11::GetSupportedMSAASampleCount( TUINT a_uiDesired ) const
{
	// Walk down from the desired count to the highest the device can do for BOTH the
	// colour render target and the depth-stencil resource. CheckMultisampleQualityLevels
	// returns 0 quality levels when a sample count is unsupported for that format.
	for ( TUINT uiCount = a_uiDesired; uiCount > 1; uiCount >>= 1 )
	{
		UINT uiColourQuality = 0;
		UINT uiDepthQuality  = 0;

		const HRESULT hrColour = m_pDevice->CheckMultisampleQualityLevels( DXGI_FORMAT_R8G8B8A8_UNORM, uiCount, &uiColourQuality );
		const HRESULT hrDepth  = m_pDevice->CheckMultisampleQualityLevels( DXGI_FORMAT_D32_FLOAT, uiCount, &uiDepthQuality );

		if ( SUCCEEDED( hrColour ) && uiColourQuality > 0 &&
		     SUCCEEDED( hrDepth ) && uiDepthQuality > 0 )
		{
			return uiCount;
		}
	}

	return 1;
}

TINT remaster::GetLinearSamplerForAddressing( Toshi::ADDRESSINGMODE a_eAddressU, Toshi::ADDRESSINGMODE a_eAddressV )
{
	using namespace Toshi;

	// Mirror is symmetric, so only check it on its own
	if ( a_eAddressU == ADDRESSINGMODE_MIRROR && a_eAddressV == ADDRESSINGMODE_MIRROR )
		return SAMPLER_LINEAR_MIRROR;

	const TBOOL bClampU = ( a_eAddressU == ADDRESSINGMODE_CLAMP );
	const TBOOL bClampV = ( a_eAddressV == ADDRESSINGMODE_CLAMP );

	if ( bClampU && bClampV )  return SAMPLER_LINEAR_CLAMP;        // clamp / clamp
	if ( !bClampU && bClampV ) return SAMPLER_LINEAR_WRAPU_CLAMPV; // wrap  / clamp
	if ( bClampU && !bClampV ) return SAMPLER_LINEAR_CLAMPU_WRAPV; // clamp / wrap

	return SAMPLER_LINEAR_WRAP;                                    // wrap / wrap (default)
}

ID3D11SamplerState* RenderDX11::CreateSamplerState( D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressU, D3D11_TEXTURE_ADDRESS_MODE addressV, D3D11_TEXTURE_ADDRESS_MODE addressW, TFLOAT mipLODBias, TUINT32 borderColor, TFLOAT minLOD, TFLOAT maxLOD, TUINT maxAnisotropy )
{
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU           = addressU;
	samplerDesc.AddressV           = addressV;
	samplerDesc.MipLODBias         = mipLODBias;
	samplerDesc.AddressW           = addressW;
	samplerDesc.BorderColor[ 0 ]   = (TFLOAT)( ( borderColor >> 24 ) & 0xFF ) / 255.0f;
	samplerDesc.BorderColor[ 1 ]   = (TFLOAT)( ( borderColor >> 16 ) & 0xFF ) / 255.0f;
	samplerDesc.BorderColor[ 2 ]   = (TFLOAT)( ( borderColor >> 8 ) & 0xFF ) / 255.0f;
	samplerDesc.BorderColor[ 3 ]   = (TFLOAT)( ( borderColor >> 0 ) & 0xFF ) / 255.0f;
	samplerDesc.MinLOD             = minLOD;
	samplerDesc.MaxLOD             = maxLOD;
	samplerDesc.Filter             = filter;
	samplerDesc.MaxAnisotropy      = maxAnisotropy;

	ID3D11SamplerState* pSamplerState;
	HRESULT             hRes = m_pDevice->CreateSamplerState( &samplerDesc, &pSamplerState );

	TASSERT( SUCCEEDED( hRes ) );

	return pSamplerState;
}

ID3D11SamplerState* RenderDX11::CreateSamplerStateAutoAnisotropy( D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressU, D3D11_TEXTURE_ADDRESS_MODE addressV, D3D11_TEXTURE_ADDRESS_MODE addressW, TFLOAT mipLODBias, TUINT32 borderColor, TFLOAT minLOD, TFLOAT maxLOD )
{
	D3D11_SAMPLER_DESC samplerDesc = {};

	if ( filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR )
	{
		filter                    = D3D11_FILTER_ANISOTROPIC;
		samplerDesc.MaxAnisotropy = 16;
	}
	else
	{
		samplerDesc.MaxAnisotropy = 1;
	}

	samplerDesc.AddressU         = addressU;
	samplerDesc.AddressV         = addressV;
	samplerDesc.MipLODBias       = mipLODBias;
	samplerDesc.AddressW         = addressW;
	samplerDesc.BorderColor[ 0 ] = (TFLOAT)( ( borderColor >> 24 ) & 0xFF ) / 255.0f;
	samplerDesc.BorderColor[ 1 ] = (TFLOAT)( ( borderColor >> 16 ) & 0xFF ) / 255.0f;
	samplerDesc.BorderColor[ 2 ] = (TFLOAT)( ( borderColor >> 8 ) & 0xFF ) / 255.0f;
	samplerDesc.BorderColor[ 3 ] = (TFLOAT)( ( borderColor >> 0 ) & 0xFF ) / 255.0f;
	samplerDesc.MinLOD           = minLOD;
	samplerDesc.MaxLOD           = maxLOD;
	samplerDesc.Filter           = filter;

	ID3D11SamplerState* pSamplerState;
	m_pDevice->CreateSamplerState( &samplerDesc, &pSamplerState );

	return pSamplerState;
}

void RenderDX11::VSBufferSetVec4( VSBufferOffset a_uiOffset, __m128 a_vData )
{
	const TUINT uiOffset = a_uiOffset * sizeof( TVector4 );
	const TUINT uiSize   = sizeof( TVector4 );
	TASSERT( uiOffset + uiSize <= VERTEX_CONSTANT_BUFFER_SIZE, "Buffer size exceeded" );

	__m128* pCurrent      = TCAST( __m128*, m_pVertexConstantBuffer ) + a_uiOffset;
	m_VertexBufferNewSize = TMath::Max( m_VertexBufferNewSize, uiOffset + uiSize );

	if ( !m_IsVertexConstantBufferUpdated )
	{
		// Make sure the data has actually been changed to reduce RAM->GPU bandwidth
		__m128 mask = _mm_cmpeq_ps( *pCurrent, a_vData );

		if ( _mm_movemask_epi8( _mm_castps_si128( mask ) ) != 0xFFFF )
		{
			_mm_store_ps( TREINTERPRETCAST( TFLOAT*, pCurrent ), a_vData );
			m_IsVertexConstantBufferUpdated = TTRUE;
		}
	}
	else
	{
		// The data has been changed before, so just copy new data
		_mm_store_ps( TREINTERPRETCAST( TFLOAT*, pCurrent ), a_vData );
	}
}

void RenderDX11::PSBufferSetVec4( PSBufferOffset a_uiOffset, __m128 a_vData )
{
	TASSERT( TFALSE );

	// 	const TUINT offset = a_uiOffset * sizeof( TVector4 );
	// 	const TUINT size   = a_iCount * sizeof( TVector4 );
	//
	// 	TASSERT( offset + size <= PIXEL_CONSTANT_BUFFER_SIZE, "Buffer size exceeded" );
	// 	TUtil::MemCopy( (TCHAR*)m_pPixelConstantBuffer + offset, a_pData, size );
	// 	m_IsPixelConstantBufferSet = TTRUE;
}

void RenderDX11::UpdateShadowCBuffer( const ShadowCBufferData& a_rData )
{
	if ( !m_pShadowConstantBuffer ) return;

	D3D11_MAPPED_SUBRESOURCE mappedSubresources;
	m_pDeviceContext->Map( m_pShadowConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresources );
	TUtil::MemCopy( mappedSubresources.pData, &a_rData, sizeof( a_rData ) );
	m_pDeviceContext->Unmap( m_pShadowConstantBuffer, 0 );
}

void RenderDX11::FlushShaders()
{
	FlushOrderTables();

	for ( auto it = TShader::sm_oShaderList.GetRootShader(); it != TNULL; it = it->GetNextShader() )
	{
		it->Flush();
	}
}

void RenderDX11::SetDstAlpha( TFLOAT a_fAlpha )
{
	if ( a_fAlpha >= 0 )
	{
		m_aCurrentBlendFactor[ 3 ]        = a_fAlpha;
		m_BlendState.Parts.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
		m_BlendState.Parts.SrcBlendAlpha  = D3D11_BLEND_BLEND_FACTOR;
		m_BlendState.Parts.DestBlendAlpha = D3D11_BLEND_ZERO;
	}
	else
	{
		m_BlendState.Parts.BlendOpAlpha   = m_BlendState.Parts.BlendOp;
		m_BlendState.Parts.SrcBlendAlpha  = m_BlendState.Parts.SrcBlend;
		m_BlendState.Parts.DestBlendAlpha = m_BlendState.Parts.DestBlend;
	}
}

void RenderDX11::SetBlendMode( TBOOL a_bBlendEnabled, D3D11_BLEND_OP a_eBlendOp, D3D11_BLEND a_eSrcBlendAlpha, D3D11_BLEND a_eDestBlendAlpha )
{
	m_BlendState.Parts.BlendOp       = a_eBlendOp;
	m_BlendState.Parts.bBlendEnabled = a_bBlendEnabled;
	m_BlendState.Parts.SrcBlend      = a_eSrcBlendAlpha;
	m_BlendState.Parts.DestBlend     = a_eDestBlendAlpha;

	if ( m_BlendState.Parts.SrcBlendAlpha != D3D11_BLEND_BLEND_FACTOR )
	{
		m_BlendState.Parts.BlendOpAlpha   = a_eBlendOp;
		m_BlendState.Parts.SrcBlendAlpha  = a_eSrcBlendAlpha;
		m_BlendState.Parts.DestBlendAlpha = a_eDestBlendAlpha;
	}
}

void RenderDX11::SetAlphaUpdate( TBOOL a_bUpdate )
{
	if ( a_bUpdate )
	{
		m_BlendState.Parts.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
	}
	else
	{
		m_BlendState.Parts.RenderTargetWriteMask &= ~D3D11_COLOR_WRITE_ENABLE_ALPHA;
	}
}

void RenderDX11::SetColorUpdate( TBOOL a_bUpdate )
{
	if ( a_bUpdate )
	{
		m_BlendState.Parts.RenderTargetWriteMask = 0b111;
	}
	else
	{
		m_BlendState.Parts.RenderTargetWriteMask = 0b000;
	}
}

void RenderDX11::SetZMode( TBOOL a_bDepthEnable, D3D11_COMPARISON_FUNC a_eComparisonFunc, D3D11_DEPTH_WRITE_MASK a_eDepthWriteMask )
{
	m_DepthState.first.Parts.bDepthEnable   = a_bDepthEnable;
	m_DepthState.first.Parts.DepthWriteMask = a_eDepthWriteMask;
	m_DepthState.first.Parts.DepthFunc      = a_eComparisonFunc;
}

void RenderDX11::SetDepthClip( TBOOL a_bClip )
{
	m_RasterizerState.Flags.Parts.bDepthClipEnable = a_bClip;
}

void RenderDX11::SetDepthBias( TINT a_iDepthBias )
{
	m_RasterizerState.DepthBias = a_iDepthBias;
}

void RenderDX11::SetSlopeScaledDepthBias( TFLOAT a_fDepthBias )
{
	m_RasterizerState.SlopeScaledDepthBias = a_fDepthBias;
}

void RenderDX11::DrawImmediately( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_iIndexCount, const void* a_pIndexData, DXGI_FORMAT a_eIndexFormat, const void* a_pVertexData, TUINT a_iStrideSize, TUINT a_iStrides )
{
	TINT iIndexSize = ( a_eIndexFormat == DXGI_FORMAT_R32_UINT ) ? 4 : ( ( a_eIndexFormat == DXGI_FORMAT_R16_UINT ) ? 2 : 0 );

	TASSERT( iIndexSize != 0 );

	// Index buffer -- ring-buffer append with NO_OVERWRITE; DISCARD on wrap or
	// first use this frame (offset == 0).
	UINT iIndexBufferSize = iIndexSize * a_iIndexCount;

	if ( ( m_iImmediateIndexCurrentOffset + iIndexBufferSize ) > IMMEDIATE_INDEX_BUFFER_SIZE )
	{
		m_iImmediateIndexCurrentOffset = 0;
	}

	TASSERT( ( m_iImmediateIndexCurrentOffset + iIndexBufferSize ) <= IMMEDIATE_INDEX_BUFFER_SIZE );

	D3D11_MAPPED_SUBRESOURCE mappedSubresource;
	m_pDeviceContext->Map( m_MainIndexBuffer, 0,
	    m_iImmediateIndexCurrentOffset == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE,
	    0, &mappedSubresource );
	Toshi::TUtil::MemCopy( (void*)( (uintptr_t)mappedSubresource.pData + m_iImmediateIndexCurrentOffset ), a_pIndexData, iIndexBufferSize );
	m_pDeviceContext->Unmap( m_MainIndexBuffer, 0 );

	// Vertex buffer -- same ring-buffer pattern.
	UINT iVertexBufferSize = a_iStrideSize * a_iStrides;

	if ( ( m_iImmediateVertexCurrentOffset + iVertexBufferSize ) > IMMEDIATE_VERTEX_BUFFER_SIZE )
	{
		m_iImmediateVertexCurrentOffset = 0;
	}

	TASSERT( ( m_iImmediateVertexCurrentOffset + iVertexBufferSize ) <= IMMEDIATE_VERTEX_BUFFER_SIZE );

	m_pDeviceContext->Map( m_MainVertexBuffer, 0,
	    m_iImmediateVertexCurrentOffset == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE,
	    0, &mappedSubresource );
	Toshi::TUtil::MemCopy( (void*)( (uintptr_t)mappedSubresource.pData + m_iImmediateVertexCurrentOffset ), a_pVertexData, iVertexBufferSize );
	m_pDeviceContext->Unmap( m_MainVertexBuffer, 0 );

	// Drawing
	UpdateRenderStates();
	SetVertexBuffer( m_MainVertexBuffer, a_iStrideSize, m_iImmediateVertexCurrentOffset );
	SetIndexBuffer( m_MainIndexBuffer, a_eIndexFormat, m_iImmediateIndexCurrentOffset );
	FlushConstantBuffers();

	SetPrimitiveTopology( a_ePrimitiveType );
	m_pDeviceContext->DrawIndexed( a_iIndexCount, 0, 0 );
	m_iImmediateIndexCurrentOffset += iIndexBufferSize;
	m_iImmediateVertexCurrentOffset += iVertexBufferSize;
}

void RenderDX11::DrawScreenRectangle()
{
	DrawScreenRectangle( GetPixelShader() );
}

void RenderDX11::DrawScreenRectangle( ID3D11PixelShader* a_pPixelShader )
{
	DrawScreenRectangle( a_pPixelShader, 0.0f, 0.0f, GetSurfaceWidth(), GetSurfaceHeight() );
}

void RenderDX11::DrawScreenRectangle( TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight )
{
	DrawScreenRectangle( GetPixelShader(), a_fX, a_fY, a_fWidth, a_fHeight );
}

void RenderDX11::DrawScreenRectangle( ID3D11PixelShader* a_pPixelShader, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight )
{
	struct ScreenVertex
	{
		Toshi::TVector2 Position;
		Toshi::TVector2 UV;
	};

	TVALIDPTR( m_pScreenRectangleVertexShader );
	TVALIDPTR( m_pScreenRectangleInputLayout );
	TVALIDPTR( a_pPixelShader );

	if ( !m_pScreenRectangleVertexShader || !m_pScreenRectangleInputLayout || !a_pPixelShader )
		return;

	const TFLOAT fSurfaceWidth  = GetSurfaceWidth();
	const TFLOAT fSurfaceHeight = GetSurfaceHeight();

	const TFLOAT fLeft   = ( a_fX / fSurfaceWidth ) * 2.0f - 1.0f;
	const TFLOAT fRight  = ( ( a_fX + a_fWidth ) / fSurfaceWidth ) * 2.0f - 1.0f;
	const TFLOAT fTop    = 1.0f - ( a_fY / fSurfaceHeight ) * 2.0f;
	const TFLOAT fBottom = 1.0f - ( ( a_fY + a_fHeight ) / fSurfaceHeight ) * 2.0f;

	const TFLOAT fUVLeft   = a_fX / fSurfaceWidth;
	const TFLOAT fUVRight  = ( a_fX + a_fWidth ) / fSurfaceWidth;
	const TFLOAT fUVTop    = a_fY / fSurfaceHeight;
	const TFLOAT fUVBottom = ( a_fY + a_fHeight ) / fSurfaceHeight;

	ScreenVertex aVertices[] = {
		{ { fLeft, fTop }, { fUVLeft, fUVTop } },
		{ { fRight, fTop }, { fUVRight, fUVTop } },
		{ { fLeft, fBottom }, { fUVLeft, fUVBottom } },
		{ { fRight, fBottom }, { fUVRight, fUVBottom } },
	};

	TUINT16 aIndices[] = { 0, 1, 2, 3 };

	SetInputLayout( m_pScreenRectangleInputLayout );
	SetVertexShader( m_pScreenRectangleVertexShader );
	SetPixelShader( a_pPixelShader );

	DrawImmediately( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, TARRAYSIZE( aIndices ), aIndices, DXGI_FORMAT_R16_UINT, aVertices, sizeof( ScreenVertex ), TARRAYSIZE( aVertices ) );
}

void RenderDX11::DrawIndexed( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_uiIndexCount, ID3D11Buffer* a_pIndexBuffer, TUINT a_uiIndexBufferOffset, DXGI_FORMAT a_eIndexBufferFormat, ID3D11Buffer* a_pVertexBuffer, TUINT a_uiStrides, TUINT a_uiOffsets, ID3D11Buffer* a_pConstantBuffer )
{
	UpdateRenderStates();
	SetVertexBuffer( a_pVertexBuffer, a_uiStrides, 0 );
	SetIndexBuffer( a_pIndexBuffer, a_eIndexBufferFormat, 0 );

	if ( !a_pConstantBuffer ) FlushConstantBuffers();
	else
	{
		VSSetConstantBuffer( 0, a_pConstantBuffer );
		PSSetConstantBuffer( 0, a_pConstantBuffer );
	}

	SetPrimitiveTopology( a_ePrimitiveType );
	m_pDeviceContext->DrawIndexed( a_uiIndexCount, a_uiIndexBufferOffset, a_uiOffsets );
}

void RenderDX11::DrawIndexedInstanced( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_uiIndexCount, TUINT a_uiInstanceCount, ID3D11Buffer* a_pIndexBuffer, TUINT a_uiIndexBufferOffset, DXGI_FORMAT a_eIndexBufferFormat, ID3D11Buffer* a_pVertexBuffer, TUINT a_uiStrides, TUINT a_uiOffsets, ID3D11Buffer* a_pConstantBuffer, TUINT a_uiStartInstanceLocation )
{
	UpdateRenderStates();
	SetVertexBuffer( a_pVertexBuffer, a_uiStrides, 0 );
	SetIndexBuffer( a_pIndexBuffer, a_eIndexBufferFormat, 0 );

	if ( !a_pConstantBuffer ) FlushConstantBuffers();
	else
	{
		VSSetConstantBuffer( 0, a_pConstantBuffer );
		PSSetConstantBuffer( 0, a_pConstantBuffer );
	}

	SetPrimitiveTopology( a_ePrimitiveType );
	m_pDeviceContext->DrawIndexedInstanced( a_uiIndexCount, a_uiInstanceCount, a_uiIndexBufferOffset, a_uiOffsets, a_uiStartInstanceLocation );
}

void RenderDX11::DrawNonIndexed( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveTopology, ID3D11Buffer* a_pVertexBuffer, TUINT a_uiVertexCount, TUINT a_uiStrides, TUINT a_uiStartVertex, TUINT a_uiOffsets, ID3D11Buffer* a_pConstantBuffer )
{
	UpdateRenderStates();
	SetVertexBuffer( a_pVertexBuffer, a_uiStrides, a_uiOffsets );
	
	if ( !a_pConstantBuffer ) FlushConstantBuffers();
	else
	{
		VSSetConstantBuffer( 0, a_pConstantBuffer );
		PSSetConstantBuffer( 0, a_pConstantBuffer );
	}

	SetPrimitiveTopology( a_ePrimitiveTopology );
	m_pDeviceContext->Draw( a_uiVertexCount, a_uiStartVertex );
}

void RenderDX11::CopyDataToTexture( ID3D11ShaderResourceView* a_pSRTex, TUINT a_uiDataSize, const void* a_pData, TUINT a_uiTextureSize )
{
	// TODO: Refactor
	UINT                     uVar1;
	UINT*                    copySize;
	TCHAR*                   _Src;
	UINT                     leftSize;
	D3D11_MAPPED_SUBRESOURCE dstPos;
	ID3D11Texture2D*         pTexture;

	pTexture = TNULL;
	a_pSRTex->GetResource( (ID3D11Resource**)&pTexture );
	m_pDeviceContext->Map( pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &dstPos );

	uVar1    = a_uiTextureSize;
	_Src     = (TCHAR*)a_pSRTex;
	leftSize = a_uiDataSize;
	while ( a_uiDataSize = leftSize, leftSize != 0 )
	{
		copySize = &a_uiDataSize;
		if ( uVar1 <= leftSize )
		{
			copySize = &a_uiTextureSize;
		}
		TUtil::MemCopy( dstPos.pData, _Src, *copySize );
		leftSize     = leftSize - uVar1;
		dstPos.pData = (void*)( (TINT)dstPos.pData + dstPos.RowPitch );
		_Src         = _Src + uVar1;
		a_uiDataSize = leftSize;
	}

	m_pDeviceContext->Unmap( pTexture, 0 );
	pTexture->Release();
}

void RenderDX11::WaitForEndOfRender()
{
	D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
	ID3D11Query*     pQuery    = NULL;

	m_pDevice->CreateQuery( &queryDesc, &pQuery );

	if ( pQuery != NULL )
	{
		m_pDeviceContext->End( pQuery );
		m_pDeviceContext->Flush();

		TINT data = 0;

		while ( data == 0 )
		{
			m_pDeviceContext->GetData( pQuery, &data, 4, 0 );
		}
	}
}

void RenderDX11::UpdateRenderStates()
{
	TPROFILER_SCOPE();

	// Update depth state if needed
	if ( m_DepthState.GetFirst().Raw != m_PreviousDepth.GetFirst().Raw || m_DepthState.GetSecond() != m_PreviousDepth.GetSecond() )
	{
		auto pFoundNode = m_DepthStatesTree.Find( m_DepthState.GetFirst() );

		ID3D11DepthStencilState* pDepthStencilState;

		if ( pFoundNode == m_DepthStatesTree.End() )
		{
			// We don't have a depth stencil state with these flags yet
			auto currentState = m_DepthState.GetFirst();

			D3D11_DEPTH_STENCIL_DESC depthStencilDesk;
			depthStencilDesk.DepthEnable                  = currentState.Parts.bDepthEnable;
			depthStencilDesk.DepthWriteMask               = (D3D11_DEPTH_WRITE_MASK)currentState.Parts.DepthWriteMask;
			depthStencilDesk.DepthFunc                    = (D3D11_COMPARISON_FUNC)currentState.Parts.DepthFunc;
			depthStencilDesk.StencilEnable                = currentState.Parts.bStencilEnable;
			depthStencilDesk.StencilReadMask              = currentState.Parts.StencilReadMask;
			depthStencilDesk.StencilWriteMask             = currentState.Parts.StencilWriteMask;
			depthStencilDesk.FrontFace.StencilFailOp      = (D3D11_STENCIL_OP)currentState.Parts.FrontFaceStencilFailOp;
			depthStencilDesk.FrontFace.StencilDepthFailOp = (D3D11_STENCIL_OP)currentState.Parts.FrontFaceStencilDepthFailOp;
			depthStencilDesk.FrontFace.StencilPassOp      = (D3D11_STENCIL_OP)currentState.Parts.FrontStencilPassOp;
			depthStencilDesk.FrontFace.StencilFunc        = (D3D11_COMPARISON_FUNC)currentState.Parts.FrontStencilFunc;
			depthStencilDesk.BackFace.StencilFailOp       = (D3D11_STENCIL_OP)currentState.Parts.BackFaceStencilFailOp;
			depthStencilDesk.BackFace.StencilDepthFailOp  = (D3D11_STENCIL_OP)currentState.Parts.BackFaceStencilDepthFailOp;
			depthStencilDesk.BackFace.StencilPassOp       = (D3D11_STENCIL_OP)currentState.Parts.BackStencilPassOp;
			depthStencilDesk.BackFace.StencilFunc         = (D3D11_COMPARISON_FUNC)currentState.Parts.BackStencilFunc;

			HRESULT hRes = m_pDevice->CreateDepthStencilState( &depthStencilDesk, &pDepthStencilState );
			TASSERT( SUCCEEDED( hRes ) );

			m_DepthStatesTree.Insert( m_DepthState.GetFirst(), pDepthStencilState );
		}
		else
		{
			// We already have a depth stencil state with these flags
			pDepthStencilState = pFoundNode->second;
		}

		m_pDeviceContext->OMSetDepthStencilState( pDepthStencilState, m_DepthState.GetSecond() );
		m_PreviousDepth = m_DepthState;
	}

	// Update rasterizer state if needed
	if ( m_RasterizerState != m_PreviousRasterizerId )
	{
		auto pFoundNode = m_RasterizersTree.Find( m_RasterizerState );

		ID3D11RasterizerState* pRasterizerState;

		if ( pFoundNode == m_RasterizersTree.End() )
		{
			// We don't have a rasterizer state with these flags yet
			D3D11_RASTERIZER_DESC rasterizerDesc;
			rasterizerDesc.DepthBiasClamp        = 0.0f;
			rasterizerDesc.AntialiasedLineEnable = FALSE;
			rasterizerDesc.MultisampleEnable     = m_RasterizerState.Flags.Parts.bMultisampleEnable;
			rasterizerDesc.DepthBias             = m_RasterizerState.DepthBias;
			rasterizerDesc.DepthClipEnable       = m_RasterizerState.Flags.Parts.bDepthClipEnable;
			rasterizerDesc.ScissorEnable         = m_RasterizerState.Flags.Parts.bScissorEnable;
			rasterizerDesc.FrontCounterClockwise = m_RasterizerState.Flags.Parts.bFrontCounterClockwise;
			rasterizerDesc.FillMode              = (D3D11_FILL_MODE)m_RasterizerState.Flags.Parts.FillMode;
			rasterizerDesc.CullMode              = (D3D11_CULL_MODE)m_RasterizerState.Flags.Parts.CullMode;
			rasterizerDesc.SlopeScaledDepthBias  = m_RasterizerState.SlopeScaledDepthBias;

			HRESULT hRes = m_pDevice->CreateRasterizerState( &rasterizerDesc, &pRasterizerState );
			TASSERT( SUCCEEDED( hRes ) );

			m_RasterizersTree.Insert( m_RasterizerState, pRasterizerState );
		}
		else
		{
			// We already have a rasterizer state with these flags
			pRasterizerState = pFoundNode->second;
		}

		m_pDeviceContext->RSSetState( pRasterizerState );
		m_PreviousRasterizerId = m_RasterizerState;
	}

	// Turn on blend if needed
	if ( m_BlendState.Parts.SrcBlendAlpha == D3D11_BLEND_BLEND_FACTOR && m_BlendState.Parts.bBlendEnabled == FALSE )
	{
		m_BlendState.Parts.bBlendEnabled = TRUE;
		m_BlendState.Parts.BlendOp       = D3D11_BLEND_OP_ADD;
		m_BlendState.Parts.SrcBlend      = D3D11_BLEND_ONE;
		m_BlendState.Parts.DestBlend     = D3D11_BLEND_ZERO;
	}

	// Update blend state if needed
	if ( m_BlendState.Raw != m_PreviousBlendState.Raw || !dx11::IsColorEqual( m_aCurrentBlendFactor, m_PreviousBlendFactor ) )
	{
		auto pFoundNode = m_BlendStatesTree.Find( m_BlendState );

		ID3D11BlendState* pBlendState;

		if ( pFoundNode == m_BlendStatesTree.End() )
		{
			// We don't have a blend state with these flags yet
			D3D11_BLEND_DESC blendDesc;

			blendDesc.AlphaToCoverageEnable  = m_BlendState.Parts.bAlphaToCoverage;
			blendDesc.IndependentBlendEnable = FALSE;

			blendDesc.RenderTarget[ 0 ].BlendOp               = m_BlendState.Parts.BlendOp;
			blendDesc.RenderTarget[ 0 ].BlendOpAlpha          = m_BlendState.Parts.BlendOpAlpha;
			blendDesc.RenderTarget[ 0 ].RenderTargetWriteMask = m_BlendState.Parts.RenderTargetWriteMask;
			blendDesc.RenderTarget[ 0 ].SrcBlend              = m_BlendState.Parts.SrcBlend;
			blendDesc.RenderTarget[ 0 ].SrcBlendAlpha         = m_BlendState.Parts.SrcBlendAlpha;
			blendDesc.RenderTarget[ 0 ].DestBlendAlpha        = m_BlendState.Parts.DestBlendAlpha;
			blendDesc.RenderTarget[ 0 ].BlendEnable           = m_BlendState.Parts.bBlendEnabled;
			blendDesc.RenderTarget[ 0 ].DestBlend             = m_BlendState.Parts.DestBlend;

			blendDesc.RenderTarget[ 1 ].BlendOp               = blendDesc.RenderTarget[ 0 ].BlendOp;
			blendDesc.RenderTarget[ 1 ].BlendOpAlpha          = blendDesc.RenderTarget[ 0 ].BlendOpAlpha;
			blendDesc.RenderTarget[ 1 ].RenderTargetWriteMask = blendDesc.RenderTarget[ 0 ].RenderTargetWriteMask;
			blendDesc.RenderTarget[ 1 ].SrcBlend              = blendDesc.RenderTarget[ 0 ].SrcBlend;
			blendDesc.RenderTarget[ 1 ].SrcBlendAlpha         = blendDesc.RenderTarget[ 0 ].SrcBlendAlpha;
			blendDesc.RenderTarget[ 1 ].DestBlendAlpha        = blendDesc.RenderTarget[ 0 ].DestBlendAlpha;
			blendDesc.RenderTarget[ 1 ].BlendEnable           = blendDesc.RenderTarget[ 0 ].BlendEnable;
			blendDesc.RenderTarget[ 1 ].DestBlend             = blendDesc.RenderTarget[ 0 ].DestBlend;

			HRESULT hRes = m_pDevice->CreateBlendState( &blendDesc, &pBlendState );
			TASSERT( SUCCEEDED( hRes ) );

			m_BlendStatesTree.Insert( m_BlendState, pBlendState );
		}
		else
		{
			// We already have a blend state with these flags
			pBlendState = pFoundNode->second;
		}

		m_pDeviceContext->OMSetBlendState( pBlendState, m_aCurrentBlendFactor, -1 );
		m_PreviousBlendState       = m_BlendState;
		m_PreviousBlendFactor[ 0 ] = m_aCurrentBlendFactor[ 0 ];
		m_PreviousBlendFactor[ 1 ] = m_aCurrentBlendFactor[ 1 ];
		m_PreviousBlendFactor[ 2 ] = m_aCurrentBlendFactor[ 2 ];
		m_PreviousBlendFactor[ 3 ] = m_aCurrentBlendFactor[ 3 ];
	}
}

void RenderDX11::FlushConstantBuffers()
{
	TPROFILER_SCOPE();

	D3D11_MAPPED_SUBRESOURCE mappedSubresources;

	// Send new data to GPU if it changed or some more data was written
	if ( m_IsVertexConstantBufferUpdated || m_VertexBufferNewSize > m_VertexBufferCurSize )
	{
		// Ping-pong buffers
		m_VertexBufferIndex = ( m_VertexBufferIndex + 1 ) % NUMBUFFERS;
		
		// Copy buffers data
		m_pDeviceContext->Map( m_VertexBuffers[ m_VertexBufferIndex ], 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresources );
		memcpy( mappedSubresources.pData, m_pVertexConstantBuffer, m_VertexBufferNewSize );
		m_pDeviceContext->Unmap( m_VertexBuffers[ m_VertexBufferIndex ], 0 );

		m_IsVertexConstantBufferUpdated = TFALSE;
		m_VertexBufferCurSize           = m_VertexBufferNewSize;
	}

	// 	if ( m_IsPixelConstantBufferSet )
	// 	{
	// 		m_PixelBufferIndex = ( m_PixelBufferIndex + 1 ) % NUMBUFFERS;
	// 		m_pDeviceContext->Map( m_PixelBuffers[ m_PixelBufferIndex ], 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresources );
	// 		memcpy( mappedSubresources.pData, m_pPixelConstantBuffer, PIXEL_CONSTANT_BUFFER_SIZE );
	// 		m_pDeviceContext->Unmap( m_PixelBuffers[ m_PixelBufferIndex ], 0 );
	// 		m_IsPixelConstantBufferSet = TFALSE;
	// 	}

	// [1/24/2026 InfiniteC0re]
	// I doubt there is a reason to use separate pixel buffer, at least for now
	TASSERT( m_IsPixelConstantBufferSet == TFALSE );
	m_VertexBufferNewSize = 0;

	VSSetConstantBuffer( 0, m_VertexBuffers[ m_VertexBufferIndex ] );
	PSSetConstantBuffer( 0, m_VertexBuffers[ m_VertexBufferIndex ] );
}

void RenderDX11::BuildAdapterDatabase()
{
	// TODO: use SDL!!!
	IDXGIFactory* pFactory = NULL;
	CreateDXGIFactory( __uuidof( IDXGIFactory ), (void**)&pFactory );

	IDXGIAdapter* pGIAdapter;
	for ( UINT i = 0; pFactory->EnumAdapters( i, &pGIAdapter ) != DXGI_ERROR_NOT_FOUND; i++ )
	{
		RenderAdapterDX11* pAdapter = new RenderAdapterDX11();

		DXGI_ADAPTER_DESC* pAdapterDesc = pAdapter->GetAdapterDesc();
		DX11_API_VALIDATE( pGIAdapter->GetDesc( pAdapterDesc ) );

		pAdapter->SetAdapterIndex( i );
		pAdapter->SetDescription( pAdapterDesc->Description );
		pAdapter->UpdateAdapterInfo();

#ifndef TOSHI_NO_LOGS
		TUtil::Log( "Adapter: %s\n", pAdapter->GetDescription() );

		TUtil::LogUp();
		TUtil::Log( "Vendor: %d, Device: %d Revision: %d\n", pAdapterDesc->VendorId, pAdapterDesc->DeviceId, pAdapterDesc->Revision );
		TUtil::Log( "DedicatedSystemMemory: %.2f MB\n", (double)pAdapterDesc->DedicatedSystemMemory / 1024 / 1024 );
		TUtil::Log( "DedicatedVideoMemory : %.2f MB\n", (double)pAdapterDesc->DedicatedVideoMemory / 1024 / 1024 );
		TUtil::Log( "SharedSystemMemory   : %.2f MB\n", (double)pAdapterDesc->SharedSystemMemory / 1024 / 1024 );
		TUtil::LogDown();
#endif // TOSHI_NO_LOGS

		pAdapter->SetDriver( "Unknown" );
		pAdapter->SetDescription( pAdapterDesc->Description );
		pAdapter->SetDriverVersionLowPart( 0 );
		pAdapter->SetDriverVersionHighPart( 0 );

		pAdapter->EnumerateOutputs( this, pGIAdapter );

		GetAdapterList()->InsertTail( pAdapter );
		pGIAdapter->Release();
	}

	if ( pFactory ) pFactory->Release();
}

#ifdef TOSHI_DEBUG
static const GUID WKPDID_D3DDebugObjectNameLocal = { 0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 } };

#  define WKPDID_D3DDebugObjectName WKPDID_D3DDebugObjectNameLocal
#endif // TOSHI_DEBUG

void RenderDX11::ShaderPipelineState::SetName( const TCHAR* a_pchName )
{
#ifdef TOSHI_DEBUG
	T2String8::Format( T2String8::ms_aScratchMem, "%s_VS", a_pchName );
	ID3D11VertexShader* pVertexShader = GetVertexShader();
	if ( pVertexShader )
		pVertexShader->SetPrivateData( WKPDID_D3DDebugObjectName, T2String8::Length( T2String8::ms_aScratchMem ), T2String8::ms_aScratchMem );

	T2String8::Format( T2String8::ms_aScratchMem, "%s_PS", a_pchName );
	ID3D11PixelShader* pPixelShader = GetPixelShader();
	if ( pPixelShader )
		pPixelShader->SetPrivateData( WKPDID_D3DDebugObjectName, T2String8::Length( T2String8::ms_aScratchMem ), T2String8::ms_aScratchMem );
#endif // TOSHI_DEBUG
}

} // namespace remaster
