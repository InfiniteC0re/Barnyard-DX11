#include "pch.h"
#include "RenderAdapterDX11.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster
{

TRenderAdapter* RenderAdapterDX11::Mode::GetAdapter() const
{
	return m_Adapter;
}

size_t RenderAdapterDX11::Mode::GetModeIndex() const
{
	return m_ModeIndex;
}

TUINT32 RenderAdapterDX11::Mode::GetWidth() const
{
	return m_Description.Width;
}

TUINT32 RenderAdapterDX11::Mode::GetHeight() const
{
	return m_Description.Height;
}

TBOOL RenderAdapterDX11::Mode::Is32Bit() const
{
	DXGI_FORMAT format = m_Description.Format;

	return (
	    format == DXGI_FORMAT_B8G8R8X8_UNORM ||
	    format == DXGI_FORMAT_R8G8B8A8_UNORM
	);
}

TBOOL RenderAdapterDX11::Mode::Is16Bit() const
{
	DXGI_FORMAT format = m_Description.Format;

	return (
	    format == DXGI_FORMAT_B5G6R5_UNORM ||
	    format == DXGI_FORMAT_B5G5R5A1_UNORM ||
	    format == DXGI_FORMAT_B4G4R4A4_UNORM
	);
}

TUINT32 RenderAdapterDX11::Mode::GetRefreshRate() const
{
	return TUINT32( TFLOAT( m_Description.RefreshRate.Numerator ) / m_Description.RefreshRate.Denominator );
}

TRenderAdapter::Mode::Device* RenderAdapterDX11::Mode::GetDevice( TUINT32 a_iDevice )
{
	TASSERT( a_iDevice >= 0 && a_iDevice < NUMSUPPORTEDDEVICES );
	return &m_Devices[ a_iDevice ];
}

void RenderAdapterDX11::Mode::GetDisplayMode( IDXGIOutput* dxgiOutput, DXGI_MODE_DESC* modeDesc )
{
	DXGI_OUTPUT_DESC outputDesc;
	dxgiOutput->GetDesc( &outputDesc );

	MONITORINFOEXA monitorInfo;
	monitorInfo.cbSize = sizeof( monitorInfo );
	GetMonitorInfoA( outputDesc.Monitor, &monitorInfo );

	DEVMODEA displaySettings = {};
	displaySettings.dmSize   = sizeof( displaySettings );
	EnumDisplaySettingsA( monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &displaySettings );

	TBOOL defaultRefreshRate = TFALSE;

	if ( displaySettings.dmDisplayFrequency == 1 || displaySettings.dmDisplayFrequency == 0 )
	{
		defaultRefreshRate                 = TTRUE;
		displaySettings.dmDisplayFrequency = 0;
	}

	DXGI_MODE_DESC matchMode;
	matchMode.Width                   = displaySettings.dmPelsWidth;
	matchMode.Height                  = displaySettings.dmPelsHeight;
	matchMode.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
	matchMode.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	matchMode.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;
	matchMode.RefreshRate.Denominator = ( defaultRefreshRate == TTRUE ) ? 0 : 1;
	matchMode.RefreshRate.Numerator   = displaySettings.dmDisplayFrequency;

	dxgiOutput->FindClosestMatchingMode( &matchMode, modeDesc, NULL );
}

Toshi::TRenderAdapter::Mode* RenderAdapterDX11::Mode::Device::GetMode() const
{
	return m_pOwnerMode;
}

TUINT32 RenderAdapterDX11::Mode::Device::GetDeviceIndex() const
{
	return m_uiDeviceIndex;
}

const TCHAR* RenderAdapterDX11::Mode::Device::GetTypeString() const
{
	return "HAL";
}

TBOOL RenderAdapterDX11::Mode::Device::IsSoftware() const
{
	return TFALSE;
}

TBOOL RenderAdapterDX11::Mode::Device::CanRenderWindowed() const
{
	return TTRUE;
}

TBOOL RenderAdapterDX11::Mode::Device::SupportsHardwareTransfomations() const
{
	return TTRUE;
}

TBOOL RenderAdapterDX11::Mode::Device::IsDepthStencilFormatSupported( TUINT32 a_iIndex ) const
{
	return TTRUE;
}

TBOOL RenderAdapterDX11::Mode::Device::SupportsPureDevice() const
{
	return TTRUE;
}

TUINT32 RenderAdapterDX11::GetAdapterIndex() const
{
	return m_AdapterIndex;
}

const Toshi::TString8& RenderAdapterDX11::GetDriver() const
{
	return m_Driver;
}

const Toshi::TString8& RenderAdapterDX11::GetDriverDescription() const
{
	return m_Description;
}

TUINT16 RenderAdapterDX11::GetProductID() const
{
	return m_DriverVersionHighPart >> 16;
}

TUINT16 RenderAdapterDX11::GetVersion() const
{
	return m_DriverVersionHighPart & 0xFFFF;
}

TUINT16 RenderAdapterDX11::GetSubVersion() const
{
	return m_DriverVersionLowPart >> 16;
}

TUINT16 RenderAdapterDX11::GetBuild() const
{
	return m_DriverVersionLowPart & 0xFFFF;
}

const void* RenderAdapterDX11::GetDeviceIdentifier() const
{
	return NULL;
}

TUINT32 RenderAdapterDX11::GetNumSupportedDevices() const
{
	return Mode::NUMSUPPORTEDDEVICES;
}

void RenderAdapterDX11::EnumerateOutputs( RenderDX11* render, IDXGIAdapter* dxgiAdapter )
{
	IDXGIOutput* dxgiOutput;
	TBOOL        shouldSaveMonitorData = TTRUE;
	GetModeList()->DeleteAll();

	int     displayIndex = 0;
	HRESULT enumResult   = dxgiAdapter->EnumOutputs( displayIndex, &dxgiOutput );

	DXGI_MODE_DESC   modeDesc;
	DXGI_OUTPUT_DESC outputDesc;
	while ( enumResult != DXGI_ERROR_NOT_FOUND )
	{
		dxgiOutput->GetDesc( &outputDesc );
		LONG deviceHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
		LONG deviceWidth  = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;

		char deviceName[ 128 ];
		TStringManager::StringUnicodeToChar( deviceName, outputDesc.DeviceName, -1 );

		TUtil::Log( "Display[%d]: %s (%dx%d)\n", displayIndex, deviceName, deviceWidth, deviceHeight );
		TUtil::LogConsole( "Display[%d]: %s (%dx%d)\n", displayIndex, deviceName, deviceWidth, deviceHeight );

		UINT numModes = 0;
		dxgiOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, NULL );

		if ( numModes > 0 )
		{
			RenderAdapterDX11::Mode::GetDisplayMode( dxgiOutput, &modeDesc );

			if ( shouldSaveMonitorData == TTRUE )
			{
				m_Mode.SetAdapter( this );
				m_Mode.SetDescription( modeDesc );
				m_Mode.SetName( outputDesc.DeviceName );
				shouldSaveMonitorData = TFALSE;
			}

			DXGI_MODE_DESC* descriptions = new DXGI_MODE_DESC[ numModes ];

			dxgiOutput->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, descriptions );

			for ( size_t i = 0; i < numModes; i++ )
			{
				Mode* mode = new Mode;
				mode->SetModeIndex( i );
				mode->SetDisplayIndex( displayIndex );
				mode->SetAdapter( this );
				mode->SetDescription( descriptions[ i ] );
				mode->SetName( outputDesc.DeviceName );

				for (TUINT k = 0; k < GetNumSupportedDevices(); k++)
				{
					auto pDevice = TSTATICCAST( Mode::Device, mode->GetDevice( k ) );

					pDevice->SetOwnerMode( mode );
					pDevice->SetDeviceIndex( k );

					pDevice->m_eFlags |= 0x00000040L; // D3DCREATE_HARDWARE_VERTEXPROCESSING
				}

				GetModeList()->InsertTail( mode );
			}

			delete[] descriptions;
		}

		displayIndex += 1;
		enumResult = dxgiAdapter->EnumOutputs( displayIndex, &dxgiOutput );
	}
}

}; // namespace remaster
