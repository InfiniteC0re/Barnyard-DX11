#include "pch.h"
#include "RenderDX11.h"
#include "RenderAdapterDX11.h"
#include "RenderContentDX11.h"
#include "RenderDX11Utils.h"
#include "RenderDX11Text.h"

#include <BYardSDK/THookedRenderD3DInterface.h>
#include <BYardSDK/SDKHooks.h>

#include <Platform/DX8/TModel_DX8.h>
#include <Platform/DX8/TTextureFactoryHAL_DX8.h>

#include <dxgi1_2.h>
#include <dwrite_3.h>

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

RenderDX11::RenderDX11()
    : m_DepthState( { 0 }, 0 )
{
	THookedRenderD3DInterface::SetSingleton( (TRenderD3DInterface*)this );

	// Legacy states (TRenderD3DInterface)
	m_pDirect3D	                         = TNULL;
	m_pDirectDevice                      = TNULL;
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
	m_pVertexConstantBuffer     = TNULL;
	m_IsVertexConstantBufferSet = TFALSE;
	TUtil::MemClear( m_PixelBuffers, sizeof( m_PixelBuffers ) );

	m_pPixelConstantBuffer     = TNULL;
	m_IsPixelConstantBufferSet = TFALSE;
	TUtil::MemClear( m_VertexBuffers, sizeof( m_VertexBuffers ) );

	m_MainVertexBuffer              = TNULL;
	m_iImmediateVertexCurrentOffset = 0;
	m_MainIndexBuffer               = TNULL;
	m_iImmediateIndexCurrentOffset  = 0;

	// Clear sampler states array
	TUtil::MemClear( m_aSamplerStates, sizeof( m_aSamplerStates ) );

	// Default blend factor
	m_aCurrentBlendFactor[ 0 ] = 1.0f;
	m_aCurrentBlendFactor[ 1 ] = 1.0f;
	m_aCurrentBlendFactor[ 2 ] = 1.0f;
	m_aCurrentBlendFactor[ 3 ] = 1.0f;

	m_PreviousBlendFactor[ 0 ] = 1.0f;
	m_PreviousBlendFactor[ 1 ] = 1.0f;
	m_PreviousBlendFactor[ 2 ] = 1.0f;
	m_PreviousBlendFactor[ 3 ] = 1.0f;

	g_pRender = this;
}

RenderDX11::~RenderDX11()
{
	g_pRender = TNULL;
}

TBOOL RenderDX11::CreateDisplay( const DISPLAYPARAMS& a_rParams )
{
	if ( !TRenderInterface::CreateDisplay() )
	{
		OnInitializationFailureDisplay();
		return TFALSE;
	}

	// Find appropriate device for the display parameters
	m_pAdapterDevice = TSTATICCAST( RenderAdapterDX11::Mode::Device, FindDevice( a_rParams ) );
	m_oDisplayParams = a_rParams;

	if ( m_pDevice )
	{
		auto pDisplayParams = GetCurrentDisplayParams();

		// Get desktop window dimensions
		RECT clientRect;
		GetClientRect( GetDesktopWindow(), &clientRect );

		// Handle large displays
		if ( 2000 < clientRect.right )
		{
			clientRect.right /= 2;
		}

		TUINT32 uiWindowPosX = 0;
		TUINT32 uiWindowPosY = 0;

		// Calculate window position for windowed mode
		if ( pDisplayParams->bWindowed )
		{
			auto pMode   = GetCurrentDevice()->GetMode();
			uiWindowPosX = ( clientRect.right - pMode->GetWidth() ) / 2;
			uiWindowPosY = ( clientRect.bottom - pMode->GetHeight() ) / 2;
		}

		// Get device information
		auto pDevice        = TSTATICCAST( RenderAdapterDX11::Mode::Device, GetCurrentDevice() );
		auto pMode          = TSTATICCAST( RenderAdapterDX11::Mode, pDevice->GetMode() );
		auto pAdapter       = TSTATICCAST( RenderAdapterDX11, pMode->GetAdapter() );
		auto uiAdapterIndex = pAdapter->GetAdapterIndex();

		// Create swapchain
		IDXGIDevice* dxgiDevice = TNULL;
		DX11_API_VALIDATE_EXIT( m_pDevice->QueryInterface( __uuidof( IDXGIDevice ), (void**)&dxgiDevice ) );

		IDXGIAdapter* dxgiAdapter = TNULL;
		DX11_API_VALIDATE_EXIT( dxgiDevice->GetAdapter( &dxgiAdapter ) );

		IDXGIFactory* dxgiFactory = TNULL;
		DX11_API_VALIDATE_EXIT( dxgiAdapter->GetParent( __uuidof( IDXGIFactory ), (void**)&dxgiFactory ) );

		m_oSwapChainDesc.BufferCount                        = 1;
		m_oSwapChainDesc.BufferDesc.Width                   = a_rParams.uiWidth;
		m_oSwapChainDesc.BufferDesc.Height                  = a_rParams.uiHeight;
		m_oSwapChainDesc.BufferDesc.RefreshRate.Numerator   = 0;
		m_oSwapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
		m_oSwapChainDesc.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;

		m_oSwapChainDesc.SampleDesc.Count = 1;
		m_oSwapChainDesc.SampleDesc.Quality = 0;

		m_oSwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		m_oSwapChainDesc.OutputWindow = m_Window.GetHWND();
		m_oSwapChainDesc.Windowed     = pDisplayParams->bWindowed;
		m_oSwapChainDesc.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;
		m_oSwapChainDesc.Flags        = 0;

		DX11_API_VALIDATE_EXIT( dxgiFactory->CreateSwapChain( m_pDevice, &m_oSwapChainDesc, &m_pSwapChain ) );

		dxgiFactory->Release();
		dxgiAdapter->Release();
		dxgiDevice->Release();

		// Create render target
		D3D11_TEXTURE2D_DESC backBufferDesc = {};
		backBufferDesc.ArraySize            = 1;
		backBufferDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		backBufferDesc.CPUAccessFlags       = 0;
		backBufferDesc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
		backBufferDesc.Height               = m_oSwapChainDesc.BufferDesc.Height;
		backBufferDesc.Width                = m_oSwapChainDesc.BufferDesc.Width;
		backBufferDesc.MipLevels            = 1;
		backBufferDesc.MiscFlags            = 0;
		backBufferDesc.SampleDesc.Count     = m_oSwapChainDesc.SampleDesc.Count;
		backBufferDesc.SampleDesc.Quality   = m_oSwapChainDesc.SampleDesc.Quality;
		backBufferDesc.Usage                = D3D11_USAGE_DEFAULT;

		DX11_API_VALIDATE_EXIT( m_pDevice->CreateTexture2D( &backBufferDesc, TNULL, &m_pRenderTargetTexture ) );
		DX11_API_VALIDATE_EXIT( m_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)&m_pRenderTargetTexture ) );
		DX11_API_VALIDATE_EXIT( m_pDevice->CreateRenderTargetView( m_pRenderTargetTexture, TNULL, &m_pRenderTargetView ) );

		// Create depth stencil view
		D3D11_TEXTURE2D_DESC depthBufferDesc = {};
		depthBufferDesc.ArraySize            = 1;
		depthBufferDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		depthBufferDesc.CPUAccessFlags       = 0;
		depthBufferDesc.Format               = DXGI_FORMAT_R24G8_TYPELESS;
		depthBufferDesc.Height               = m_oSwapChainDesc.BufferDesc.Height;
		depthBufferDesc.Width                = m_oSwapChainDesc.BufferDesc.Width;
		depthBufferDesc.MipLevels            = 1;
		depthBufferDesc.MiscFlags            = 0;
		depthBufferDesc.SampleDesc.Count     = m_oSwapChainDesc.SampleDesc.Count;
		depthBufferDesc.SampleDesc.Quality   = m_oSwapChainDesc.SampleDesc.Quality;
		depthBufferDesc.Usage                = D3D11_USAGE_DEFAULT;

		D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};

		depthStencilDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilDesc.Flags              = 0;
		depthStencilDesc.Texture2D.MipSlice = 0;
		depthStencilDesc.ViewDimension      = m_oSwapChainDesc.SampleDesc.Count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;

		DX11_API_VALIDATE_EXIT( m_pDevice->CreateTexture2D( &depthBufferDesc, TNULL, &m_pDepthStencilTexture ) );
		DX11_API_VALIDATE_EXIT( m_pDevice->CreateDepthStencilView( m_pDepthStencilTexture, &depthStencilDesc, &m_pDepthStencilView ) );

		// Set window mode
		if ( pDisplayParams->bWindowed )
		{
			m_Window.SetWindowed();
		}
		else
		{
			m_Window.SetFullscreen();
		}

		s_pRenderHeap = g_pMemory->CreateMemBlock( HEAPSIZE, "RenderDX11", TNULL, 0 );
		CreateRenderObjects();

		// Handle multi-monitor setup
		if ( uiAdapterIndex != 0 )
		{
			HMONITOR hMonitor = m_pDirect3D->GetAdapterMonitor( uiAdapterIndex );

			MONITORINFO monitorInfo = { .cbSize = sizeof( monitorInfo ) };
			GetMonitorInfoA( hMonitor, &monitorInfo );

			uiWindowPosX += monitorInfo.rcMonitor.left;
			uiWindowPosY += monitorInfo.rcMonitor.right;
		}

		RECT oAdjustedWindowSize;
		oAdjustedWindowSize.left   = 0;
		oAdjustedWindowSize.top    = 0;
		oAdjustedWindowSize.right  = pDisplayParams->uiWidth;
		oAdjustedWindowSize.bottom = pDisplayParams->uiHeight;
		AdjustWindowRect( &oAdjustedWindowSize, 0x86c00000, FALSE );

		// Set window position and size
		m_Window.SetPosition( uiWindowPosX, uiWindowPosY, oAdjustedWindowSize.right - oAdjustedWindowSize.left, oAdjustedWindowSize.bottom - oAdjustedWindowSize.top );

		// Set cursor position to center of window
		SetCursorPos(
		    uiWindowPosX + pDisplayParams->uiWidth / 2,
		    uiWindowPosY + pDisplayParams->uiHeight / 2
		);

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

		// Initialize Direct2D and DirectWrite
		D2D1CreateFactory( D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory );
		DWriteCreateFactory( DWRITE_FACTORY_TYPE_SHARED, __uuidof( IDWriteFactory ), (IUnknown**)&m_pDWFactory );

		IDXGISurface* pBackBufferSurface = nullptr;
		m_pRenderTargetTexture->QueryInterface( __uuidof( IDXGISurface ), (void**)&pBackBufferSurface );

		D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
		    D2D1_RENDER_TARGET_TYPE_DEFAULT,
		    D2D1::PixelFormat( DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED )
		);

		//DX11_API_VALIDATE( m_pD2DFactory->CreateDxgiSurfaceRenderTarget( pBackBufferSurface, &rtProps, &m_pD2DRenderTarget ) );

		// Create font
		DX11_API_VALIDATE( m_pDWFactory->CreateFontFileReference(
			L"CCThatsAllFolks.ttf",
			TNULL,
			&m_pDWFontFile
		) );
		DX11_API_VALIDATE( m_pDWFactory->CreateFontFace(
		    DWRITE_FONT_FACE_TYPE_TRUETYPE,
		    1,
		    &m_pDWFontFile,
		    0,
		    DWRITE_FONT_SIMULATIONS_NONE,
		    &m_pDWFontFace
		) );

		pBackBufferSurface->Release();

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
	FlushDyingResources();
	m_Window.Update( a_fDeltaTime );

	return !m_bExited;
}

TBOOL RenderDX11::BeginScene()
{
	if ( BaseClass::BeginScene() )
	{
		TFLOAT clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
		m_pDeviceContext->OMSetRenderTargets( 1, &m_pRenderTargetView, m_pDepthStencilView );
		m_pDeviceContext->ClearRenderTargetView( m_pRenderTargetView, clearColor );

		D3D11_VIEWPORT viewport;
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.Width    = TFLOAT( m_oSwapChainDesc.BufferDesc.Width );
		viewport.Height   = TFLOAT( m_oSwapChainDesc.BufferDesc.Height );

		m_pDeviceContext->RSSetViewports( 1, &viewport );
		m_bInScene = TTRUE;

		return TTRUE;
	}
	
	return TFALSE;
}

TBOOL RenderDX11::EndScene()
{
	{
		/*ID2D1Geometry* pTextGeometry = dx11::CreateTextGeometry( L"HELLO WORLD!" );

		m_pD2DRenderTarget->BeginDraw();

		ID2D1SolidColorBrush* pBlackBrush = TNULL;
		m_pD2DRenderTarget->CreateSolidColorBrush( D2D1::ColorF( D2D1::ColorF::Black, 1.0f ), &pBlackBrush );

		ID2D1SolidColorBrush* pWhiteBrush = TNULL;
		m_pD2DRenderTarget->CreateSolidColorBrush( D2D1::ColorF( D2D1::ColorF::White, 1.0f ), &pWhiteBrush );

		ID2D1StrokeStyle* pStrokeStyle;
		m_pD2DFactory->CreateStrokeStyle(
		    D2D1::StrokeStyleProperties(
		        D2D1_CAP_STYLE_ROUND,
		        D2D1_CAP_STYLE_ROUND,
		        D2D1_CAP_STYLE_ROUND,
		        D2D1_LINE_JOIN_ROUND,
		        0.0f,
		        D2D1_DASH_STYLE_SOLID,
		        0.0f
		    ),
		    TNULL,
		    0,
		    &pStrokeStyle
		);

		m_pD2DRenderTarget->SetTransform( D2D1::Matrix3x2F::Translation( 10.0f, 65.0f ) );
		m_pD2DRenderTarget->DrawGeometry( pTextGeometry, pBlackBrush, 6.0f, pStrokeStyle );
		m_pD2DRenderTarget->FillGeometry( pTextGeometry, pWhiteBrush );

		pBlackBrush->Release();
		pWhiteBrush->Release();
		pStrokeStyle->Release();
		m_pD2DRenderTarget->EndDraw();

		pTextGeometry->Release();*/

		// Prepare Text Resources
		//IDWriteTextFormat* pTextFormat = nullptr;
		//m_pDWFactory->CreateTextFormat(
		//    L"Arial",
		//    nullptr,
		//    DWRITE_FONT_WEIGHT_NORMAL,
		//    DWRITE_FONT_STYLE_NORMAL,
		//    DWRITE_FONT_STRETCH_NORMAL,
		//    24.0f,
		//    L"en-us",
		//    &pTextFormat
		//);

		//IDWriteTextLayout* pTextLayout = nullptr;
		//const WCHAR*       text        = L"Привет";
		//m_pDWFactory->CreateTextLayout(
		//    text,
		//    wcslen( text ),
		//    pTextFormat,
		//    500.0f, // Max width
		//    100.0f, // Max height
		//    &pTextLayout
		//);

		//// ... (Direct3D 11 rendering) ...

		//// Render Text with Direct2D
		//m_pD2DRenderTarget->BeginDraw();
		//ID2D1SolidColorBrush* pBlackBrush = nullptr;
		//m_pD2DRenderTarget->CreateSolidColorBrush( D2D1::ColorF( D2D1::ColorF::White, 0.5f ), &pBlackBrush );
		//m_pD2DRenderTarget->DrawTextLayout( D2D1::Point2F( 10.0f, 10.0f ), pTextLayout, pBlackBrush );


		//
		//m_pD2DRenderTarget->EndDraw();

		//pBlackBrush->Release();
		//pTextLayout->Release();
		//pTextFormat->Release();
	}

	m_pSwapChain->Present( 1, 0 );
	m_bInScene = TFALSE;

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
	return CALL_THIS( 0x006c6320, RenderDX11*, TModel*, this, const TCHAR*, a_szFilePath, TBOOL, a_bLoad, TTRB*, a_pAssetTRB, TUINT8, a_ui8FileNameLen );
}

TDebugText* RenderDX11::CreateDebugText()
{
	throw std::logic_error( "The method or operation is not implemented." );
}

void RenderDX11::DestroyDebugText()
{
	throw std::logic_error( "The method or operation is not implemented." );
}

TBOOL RenderDX11::RecreateDisplay( const DISPLAYPARAMS& a_rDisplayParams )
{
	return TTRUE;
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
		TUINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT
#if defined( TOSHI_DEBUG )
		    | D3D11_CREATE_DEVICE_DEBUG
#endif
			;

		BuildAdapterDatabase();
		DX11_API_VALIDATE_EXIT( D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0, D3D11_SDK_VERSION, &m_pDevice, &m_eFeatureLevel, &m_pDeviceContext ) );

		return m_pDevice && m_pDeviceContext && m_Window.Create( this, TString8::VarArgs( "%s - DirectX11", a_pchWindowTitle ) );
	}

	return TFALSE;
}

void RenderDX11::CreateRenderObjects()
{
	// Sample states
	m_aSamplerStates[ 0 ]  = CreateSamplerState( D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ 1 ]  = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ 2 ]  = CreateSamplerState( D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ 3 ]  = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ 4 ]  = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_MIRROR, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ 5 ]  = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ 6 ]  = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ 7 ]  = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, -1.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ 8 ]  = CreateSamplerState( D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ 9 ]  = CreateSamplerState( D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );
	m_aSamplerStates[ 10 ] = CreateSamplerStateAutoAnisotropy( D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX );
	m_aSamplerStates[ 11 ] = CreateSamplerState( D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, 0.0f, 0, 0.0f, D3D11_FLOAT32_MAX, 1 );

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

	m_pVertexConstantBuffer     = TMalloc( VERTEX_CONSTANT_BUFFER_SIZE, s_pRenderHeap );
	m_IsVertexConstantBufferSet = TFALSE;
	m_VertexBufferIndex         = 0;

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
	m_PixelBufferIndex         = 0;

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
	m_BlendState.Parts.Unknown2              = 1;

	// Rasterizer state
	m_RasterizerState.Flags.Parts.FillMode               = D3D11_FILL_SOLID;
	m_RasterizerState.Flags.Parts.CullMode               = D3D11_CULL_BACK;
	m_RasterizerState.Flags.Parts.bFrontCounterClockwise = FALSE;
	m_RasterizerState.Flags.Parts.bDepthClipEnable       = TRUE;
	m_RasterizerState.Flags.Parts.bScissorEnable         = FALSE;
	m_RasterizerState.Flags.Parts.bMultisampleEnable     = FALSE;
	m_RasterizerState.DepthBias                          = 0;
	m_RasterizerState.SlopeScaledDepthBias               = 0.0f;
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
	HRESULT hRes = m_pDevice->CreateSamplerState( &samplerDesc, &pSamplerState );

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

void RenderDX11::VSBufferSetVec4( VSBufferOffset a_uiOffset, const void* a_pData, TINT a_iCount /*= 1 */ )
{
	TUINT offset = a_uiOffset * 16;
	TUINT size   = a_iCount * 16;

	TASSERT( offset + size <= VERTEX_CONSTANT_BUFFER_SIZE, "Buffer size exceeded" );
	TUtil::MemCopy( (TCHAR*)m_pVertexConstantBuffer + offset, a_pData, size );
	m_IsVertexConstantBufferSet = TTRUE;
}

void RenderDX11::PSBufferSetVec4( PSBufferOffset a_uiOffset, const void* a_pData, TINT a_iCount /*= 1 */ )
{
	TUINT offset = a_uiOffset * 16;
	TUINT size   = a_iCount * 16;

	TASSERT( offset + size <= PIXEL_CONSTANT_BUFFER_SIZE, "Buffer size exceeded" );
	TUtil::MemCopy( (TCHAR*)m_pPixelConstantBuffer + offset, a_pData, size );
	m_IsPixelConstantBufferSet = TTRUE;
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

void RenderDX11::DrawImmediately( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_iIndexCount, const void* a_pIndexData, DXGI_FORMAT a_eIndexFormat, const void* a_pVertexData, TUINT a_iStrideSize, TUINT a_iStrides )
{
	TINT iIndexSize = ( a_eIndexFormat == DXGI_FORMAT_R32_UINT ) ? 4 : ( ( a_eIndexFormat == DXGI_FORMAT_R16_UINT ) ? 2 : 0 );
	
	TASSERT( iIndexSize != 0 );

	// Index buffer
	UINT iIndexBufferSize = iIndexSize * a_iIndexCount;

	if ( ( m_iImmediateIndexCurrentOffset + iIndexBufferSize ) <= IMMEDIATE_INDEX_BUFFER_SIZE )
	{
		m_iImmediateIndexCurrentOffset = 0;
	}

	TASSERT( ( m_iImmediateIndexCurrentOffset + iIndexBufferSize ) <= IMMEDIATE_INDEX_BUFFER_SIZE );

	D3D11_MAPPED_SUBRESOURCE mappedSubresource;
	m_pDeviceContext->Map( m_MainIndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource );
	Toshi::TUtil::MemCopy( (void*)( (uintptr_t)mappedSubresource.pData + m_iImmediateIndexCurrentOffset ), a_pIndexData, iIndexBufferSize );
	m_pDeviceContext->Unmap( m_MainIndexBuffer, 0 );

	// Vertex buffer
	UINT iVertexBufferSize = a_iStrideSize * a_iStrides;

	if ( ( m_iImmediateVertexCurrentOffset + iVertexBufferSize ) <= IMMEDIATE_VERTEX_BUFFER_SIZE )
	{
		m_iImmediateVertexCurrentOffset = 0;
	}

	TASSERT( ( m_iImmediateVertexCurrentOffset + iVertexBufferSize ) <= IMMEDIATE_VERTEX_BUFFER_SIZE );

	m_pDeviceContext->Map( m_MainVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource );
	Toshi::TUtil::MemCopy( (void*)( (uintptr_t)mappedSubresource.pData + m_iImmediateVertexCurrentOffset ), a_pVertexData, iVertexBufferSize );
	m_pDeviceContext->Unmap( m_MainVertexBuffer, 0 );

	// Drawing
	UpdateRenderStates();
	m_pDeviceContext->IASetVertexBuffers( 0, 1, &m_MainVertexBuffer, &a_iStrideSize, &m_iImmediateVertexCurrentOffset );
	m_pDeviceContext->IASetIndexBuffer( m_MainIndexBuffer, a_eIndexFormat, m_iImmediateIndexCurrentOffset );
	FlushConstantBuffers();
	m_pDeviceContext->IASetPrimitiveTopology( a_ePrimitiveType );
	m_pDeviceContext->DrawIndexed( a_iIndexCount, 0, 0 );
	m_iImmediateIndexCurrentOffset += iIndexBufferSize;
	m_iImmediateVertexCurrentOffset += iVertexBufferSize;
}

void RenderDX11::DrawIndexed( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_uiIndexCount, ID3D11Buffer* a_pIndexBuffer, TUINT a_uiIndexBufferOffset, DXGI_FORMAT a_eIndexBufferFormat, ID3D11Buffer* a_pVertexBuffer, TUINT a_uiStrides, TUINT a_uiOffsets )
{
	UpdateRenderStates();
	m_pDeviceContext->IASetVertexBuffers( 0, 1, &a_pVertexBuffer, &a_uiStrides, &a_uiOffsets );
	m_pDeviceContext->IASetIndexBuffer( a_pIndexBuffer, a_eIndexBufferFormat, a_uiIndexBufferOffset );
	FlushConstantBuffers();

	m_pDeviceContext->IASetPrimitiveTopology( a_ePrimitiveType );
	m_pDeviceContext->DrawIndexed( a_uiIndexCount, 0, 0 );
}

void RenderDX11::DrawNonIndexed( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveTopology, ID3D11Buffer* a_pVertexBuffer, TUINT a_uiVertexCount, TUINT a_uiStrides, TUINT a_uiStartVertex, TUINT a_uiOffsets )
{
	UpdateRenderStates();
	m_pDeviceContext->IASetVertexBuffers( 0, 1, &a_pVertexBuffer, &a_uiStrides, &a_uiOffsets );
	FlushConstantBuffers();
	m_pDeviceContext->IASetPrimitiveTopology( a_ePrimitiveTopology );
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

void RenderDX11::SetSamplerState( TUINT a_uiStartSlot, TINT a_iSamplerId, BOOL a_bSetForPS )
{
	auto pSampler = m_aSamplerStates[ a_iSamplerId ];

	if ( a_bSetForPS == TRUE )
	{
		m_pDeviceContext->PSSetSamplers( a_uiStartSlot, 1, &pSampler );
	}
	else
	{
		m_pDeviceContext->VSSetSamplers( a_uiStartSlot, 1, &pSampler );
	}
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

			blendDesc.AlphaToCoverageEnable  = FALSE;
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
	D3D11_MAPPED_SUBRESOURCE mappedSubresources;

	if ( m_IsVertexConstantBufferSet )
	{
		m_VertexBufferIndex = ( m_VertexBufferIndex + 1 ) % NUMBUFFERS;
		m_pDeviceContext->Map( m_VertexBuffers[ m_VertexBufferIndex ], 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresources );
		memcpy( mappedSubresources.pData, m_pVertexConstantBuffer, VERTEX_CONSTANT_BUFFER_SIZE );
		m_pDeviceContext->Unmap( m_VertexBuffers[ m_VertexBufferIndex ], 0 );
		m_IsVertexConstantBufferSet = TFALSE;
	}

	if ( m_IsPixelConstantBufferSet )
	{
		m_PixelBufferIndex = ( m_PixelBufferIndex + 1 ) % NUMBUFFERS;
		m_pDeviceContext->Map( m_PixelBuffers[ m_PixelBufferIndex ], 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresources );
		memcpy( mappedSubresources.pData, m_pPixelConstantBuffer, PIXEL_CONSTANT_BUFFER_SIZE );
		m_pDeviceContext->Unmap( m_PixelBuffers[ m_PixelBufferIndex ], 0 );
		m_IsPixelConstantBufferSet = TFALSE;
	}

	m_pDeviceContext->VSSetConstantBuffers( 0, 1, &m_VertexBuffers[ m_VertexBufferIndex ] );
	m_pDeviceContext->PSSetConstantBuffers( 0, 1, &m_PixelBuffers[ m_PixelBufferIndex ] );
}

void RenderDX11::BuildAdapterDatabase()
{
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

		TUtil::Log( "Adapter: %s\n", pAdapter->GetDescription() );
		TUtil::LogUp();
		TUtil::Log( "Vendor: %d, Device: %d Revision: %d\n", pAdapterDesc->VendorId, pAdapterDesc->DeviceId, pAdapterDesc->Revision );
		TUtil::Log( "DedicatedSystemMemory: %.2f MB\n", (double)pAdapterDesc->DedicatedSystemMemory / 1024 / 1024 );
		TUtil::Log( "DedicatedVideoMemory : %.2f MB\n", (double)pAdapterDesc->DedicatedVideoMemory / 1024 / 1024 );
		TUtil::Log( "SharedSystemMemory   : %.2f MB\n", (double)pAdapterDesc->SharedSystemMemory / 1024 / 1024 );

		TUtil::LogDown();

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

} // namespace remaster
