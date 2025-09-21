#include "pch.h"
#include "ERRender.h"
#include "RenderAdapterDX11.h"

#include <BYardSDK/THookedRenderD3DInterface.h>
#include <BYardSDK/SDKHooks.h>

#include <Platform/DX8/TModel_DX8.h>

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

RenderDX11::RenderDX11()
{
	THookedRenderD3DInterface::SetSingleton( (TRenderD3DInterface*)this );

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

		//// Initialize presentation parameters
		//TUtil::MemClear( &m_PresentParams, sizeof( m_PresentParams ) );
		//m_PresentParams.Windowed               = pDisplayParams->bWindowed;
		//m_PresentParams.BackBufferCount        = 1;
		//m_PresentParams.MultiSampleType        = D3DMULTISAMPLE_NONE;
		//m_PresentParams.SwapEffect             = D3DSWAPEFFECT_DISCARD;
		//m_PresentParams.EnableAutoDepthStencil = TRUE;
		//m_PresentParams.hDeviceWindow          = m_Window.GetHWND();
		//m_PresentParams.AutoDepthStencilFormat = TD3DAdapter::Mode::Device::DEPTHSTENCILFORMATS[ pDisplayParams->eDepthStencilFormat ];
		//m_PresentParams.BackBufferWidth        = pDisplayParams->uiWidth;
		//m_PresentParams.BackBufferHeight       = pDisplayParams->uiHeight;

		// Get device information
		auto pDevice        = TSTATICCAST( RenderAdapterDX11::Mode::Device, GetCurrentDevice() );
		auto pMode          = TSTATICCAST( RenderAdapterDX11::Mode, pDevice->GetMode() );
		auto pAdapter       = TSTATICCAST( RenderAdapterDX11, pMode->GetAdapter() );
		auto uiAdapterIndex = pAdapter->GetAdapterIndex();

		//// Set back buffer format based on windowed/fullscreen mode
		//if ( pDisplayParams->bWindowed )
		//{
		//	m_PresentParams.BackBufferFormat = pMode->GetD3DDisplayMode().Format;
		//}
		//else
		//{
		//	m_PresentParams.BackBufferFormat = pMode->GetBackBufferFormat( pDisplayParams->uiColourDepth );
		//}

		//// Create Direct3D device
		//HRESULT hRes = m_pDirect3D->CreateDevice(
		//    uiAdapterIndex,
		//    TD3DAdapter::Mode::Device::DEVICETYPES[ pDevice->GetDeviceIndex() ],
		//    m_Window.GetHWND(),
		//    pDevice->GetD3DDeviceFlags(),
		//    &m_PresentParams,
		//    &m_pDirectDevice
		//);

		//if ( FAILED( hRes ) )
		//{
		//	OnInitializationFailureDevice();
		//	PrintError( hRes, "Failed to create D3D Device!" );
		//	return TFALSE;
		//}

		//// Initialize device states
		//SetDeviceDefaultStates();

		// Set window mode
		if ( pDisplayParams->bWindowed )
		{
			m_Window.SetWindowed();
		}
		else
		{
			m_Window.SetFullscreen();
		}

		// Handle multi-monitor setup
		/*if ( uiAdapterIndex != 0 )
		{
			HMONITOR hMonitor = m_pDirect3D->GetAdapterMonitor( uiAdapterIndex );

			MONITORINFO monitorInfo = { .cbSize = sizeof( monitorInfo ) };
			GetMonitorInfoA( hMonitor, &monitorInfo );

			uiWindowPosX += monitorInfo.rcMonitor.left;
			uiWindowPosY += monitorInfo.rcMonitor.right;
		}*/

		// Set window position and size
		m_Window.SetPosition( uiWindowPosX, uiWindowPosY, pDisplayParams->uiWidth, pDisplayParams->uiHeight );

		//// Get back buffer surface description
		//IDirect3DSurface8* pSurface;
		//m_pDirectDevice->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, &pSurface );
		//pSurface->GetDesc( &m_SurfaceDesk );
		//pSurface->Release();

		// Set cursor position to center of window
		SetCursorPos(
		    uiWindowPosX + pDisplayParams->uiWidth / 2,
		    uiWindowPosY + pDisplayParams->uiHeight / 2
		);

		//m_pDirectDevice->ShowCursor( TRUE );

		//// Create invalid texture pattern
		//TUINT invalidTextureData[ 32 ];
		//for ( TINT i = 0; i < 32; i++ )
		//{
		//	invalidTextureData[ i ] = 0xff0fff0f;
		//}

		//auto pTextureFactory = GetSystemResource<TTextureFactoryHAL>( SYSRESOURCE_TEXTUREFACTORY );
		//m_pInvalidTexture    = pTextureFactory->CreateTextureFromMemory( invalidTextureData, sizeof( invalidTextureData ), 0x11, 8, 8 );

		// Enable color correction and mark display as created
		EnableColourCorrection( TTRUE );
		m_bDisplayCreated = TTRUE;

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
	return TTRUE;
}

TBOOL RenderDX11::EndScene()
{
	return TTRUE;
	//throw std::logic_error( "The method or operation is not implemented." );
}

TRenderAdapter::Mode::Device* RenderDX11::GetCurrentDevice()
{
	return m_pAdapterDevice;
}

TRenderInterface::DISPLAYPARAMS* RenderDX11::GetCurrentDisplayParams()
{
	return &m_oDisplayParams;
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
	/*auto pModel = new TModelHAL();

	if ( pModel )
	{
		if ( !pModel->Create( a_pTMD, a_bLoad ) )
		{
			pModel->Delete();
			return TNULL;
		}
	}

	return pModel;*/
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
		TUINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED
#if defined( TOSHI_DEBUG )
		    | D3D11_CREATE_DEVICE_DEBUG
#endif
			;

		BuildAdapterDatabase();
		DX11_API_VALIDATE_EXIT( D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, 0, D3D11_SDK_VERSION, &m_pDevice, &m_eFeatureLevel, &m_pDeviceContext ) );

		return m_pDevice && m_pDeviceContext && m_Window.Create( this, a_pchWindowTitle );
	}

	return TFALSE;
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

RenderContextD3D11::RenderContextD3D11( TRenderInterface* a_pRenderer )
    : Toshi::TRenderContext( a_pRenderer )
{
}

RenderContextD3D11::~RenderContextD3D11()
{
}

void RenderContextD3D11::Update()
{
	if ( IsDirty() )
	{
		if ( m_eCameraMode == CameraMode_Perspective )
		{
			ComputePerspectiveProjection();
			ComputePerspectiveFrustum();
		}
		else
		{
			ComputeOrthographicProjection();
			ComputeOrthographicFrustum();
		}

		/*auto pRenderer = TSTATICCAST( TRenderD3DInterface, m_pRenderer );
		pRenderer->GetDirect3DDevice()->SetTransform( D3DTS_VIEW, TREINTERPRETCAST( D3DMATRIX*, &TMatrix44::IDENTITY ) );
		pRenderer->GetDirect3DDevice()->SetTransform( D3DTS_PROJECTION, TREINTERPRETCAST( D3DMATRIX*, &m_Projection ) );*/
	}
}

void RenderContextD3D11::ComputePerspectiveProjection()
{
	TRenderContext::ComputePerspectiveProjection( m_Projection, m_oViewportParams, m_oProjParams );
}

void RenderContextD3D11::ComputeOrthographicProjection()
{
	TRenderContext::ComputeOrthographicProjection( m_Projection, m_oViewportParams, m_oProjParams );
}

void RenderContextD3D11::ComputePerspectiveFrustum()
{
	TRenderContext::ComputePerspectiveFrustum( m_aFrustumPlanes1, m_oViewportParams, m_oProjParams );

	// Copy planes
	m_aFrustumPlanes2[ WORLDPLANE_LEFT ]   = m_aFrustumPlanes1[ WORLDPLANE_LEFT ];
	m_aFrustumPlanes2[ WORLDPLANE_RIGHT ]  = m_aFrustumPlanes1[ WORLDPLANE_RIGHT ];
	m_aFrustumPlanes2[ WORLDPLANE_BOTTOM ] = m_aFrustumPlanes1[ WORLDPLANE_BOTTOM ];
	m_aFrustumPlanes2[ WORLDPLANE_TOP ]    = m_aFrustumPlanes1[ WORLDPLANE_TOP ];
	m_aFrustumPlanes2[ WORLDPLANE_NEAR ]   = m_aFrustumPlanes1[ WORLDPLANE_NEAR ];
	m_aFrustumPlanes2[ WORLDPLANE_FAR ]    = m_aFrustumPlanes1[ WORLDPLANE_FAR ];
}

void RenderContextD3D11::ComputeOrthographicFrustum()
{
	TRenderContext::ComputeOrthographicFrustum( m_aFrustumPlanes1, m_oViewportParams, m_oProjParams );

	// Copy planes
	m_aFrustumPlanes2[ WORLDPLANE_LEFT ]   = m_aFrustumPlanes1[ WORLDPLANE_LEFT ];
	m_aFrustumPlanes2[ WORLDPLANE_RIGHT ]  = m_aFrustumPlanes1[ WORLDPLANE_RIGHT ];
	m_aFrustumPlanes2[ WORLDPLANE_BOTTOM ] = m_aFrustumPlanes1[ WORLDPLANE_BOTTOM ];
	m_aFrustumPlanes2[ WORLDPLANE_TOP ]    = m_aFrustumPlanes1[ WORLDPLANE_TOP ];
	m_aFrustumPlanes2[ WORLDPLANE_NEAR ]   = m_aFrustumPlanes1[ WORLDPLANE_NEAR ];
	m_aFrustumPlanes2[ WORLDPLANE_FAR ]    = m_aFrustumPlanes1[ WORLDPLANE_FAR ];
}

} // namespace remaster
