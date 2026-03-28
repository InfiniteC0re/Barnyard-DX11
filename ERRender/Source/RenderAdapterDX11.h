#pragma once
#include <Render/TRenderAdapter.h>
#include <Toshi/T2String.h>
#include <Toshi/TString16.h>

#include <d3d11.h>

namespace remaster
{

class RenderDX11;

class RenderAdapterDX11 : public Toshi::TRenderAdapter
{
public:
	class Mode : public Toshi::TRenderAdapter::Mode
	{
	public:
		static constexpr size_t NUMSUPPORTEDDEVICES = 2;
		static constexpr size_t DISPLAYNAMESIZE     = 45;

		class Device : public Toshi::TRenderAdapter::Mode::Device
		{
		public:
			friend RenderAdapterDX11;
			friend Mode;

		public:
			virtual Toshi::TRenderAdapter::Mode* GetMode() const override;
			virtual TUINT32                      GetDeviceIndex() const override;
			virtual const TCHAR*                 GetTypeString() const override;
			virtual TBOOL                        IsSoftware() const override;
			virtual TBOOL                        CanRenderWindowed() const override;
			virtual TBOOL                        SupportsHardwareTransfomations() const override;
			virtual TBOOL                        IsDepthStencilFormatSupported( TUINT32 a_iIndex ) const override;
			virtual TBOOL                        SupportsPureDevice() const;

			void SetOwnerMode( Mode* a_pMode ) { m_pOwnerMode = a_pMode; }
			void SetDeviceIndex( TUINT32 a_uiIndex ) { m_uiDeviceIndex = a_uiIndex; }

		private:
			TBYTE   PADDING1[ 212 ];
			TUINT32 m_eFlags;
			Mode*   m_pOwnerMode;
			TUINT32 m_uiDeviceIndex;
			TBYTE   PADDING2[ 16 ];
		};

	public:
		Mode() : m_Devices() {}
		~Mode() = default;

		virtual Toshi::TRenderAdapter*               GetAdapter() const OVERRIDE;
		virtual size_t                               GetModeIndex() const OVERRIDE;
		virtual TUINT32                              GetWidth() const OVERRIDE;
		virtual TUINT32                              GetHeight() const OVERRIDE;
		virtual TBOOL                                Is32Bit() const OVERRIDE;
		virtual TBOOL                                Is16Bit() const OVERRIDE;
		virtual TUINT32                              GetRefreshRate() const OVERRIDE;
		virtual Toshi::TRenderAdapter::Mode::Device* GetDevice( TUINT32 a_iDevice ) OVERRIDE;

		static void GetDisplayMode( IDXGIOutput* dxgiOutput, DXGI_MODE_DESC* modeDesc );

		DXGI_MODE_DESC* GetDescription()
		{
			return &m_Description;
		}

		void SetDescription( DXGI_MODE_DESC& description )
		{
			m_Description = description;
		}

		void SetAdapter( RenderAdapterDX11* adapter )
		{
			m_Adapter = adapter;
		}

		WCHAR* GetDisplayName()
		{
			return m_DisplayName;
		}

		void SetModeIndex( size_t index )
		{
			m_ModeIndex = index;
		}

		void SetDisplayIndex( size_t index )
		{
			m_DisplayIndex = index;
		}

		void SetName( WCHAR* name )
		{
			Toshi::TStringManager::String16Copy( GetDisplayName(), name );
		}

	private:
		RenderAdapterDX11* m_Adapter;
		DXGI_MODE_DESC     m_Description;
		size_t             m_ModeIndex;
		Device             m_Devices[ NUMSUPPORTEDDEVICES ];
		size_t             m_DisplayIndex;
		WCHAR              m_DisplayName[ DISPLAYNAMESIZE + 1 ];
	};

public:
	RenderAdapterDX11() {}

	virtual TUINT32                GetAdapterIndex() const OVERRIDE;
	virtual const Toshi::TString8& GetDriver() const OVERRIDE;
	virtual const Toshi::TString8& GetDriverDescription() const OVERRIDE;
	virtual TUINT16                GetProductID() const OVERRIDE;
	virtual TUINT16                GetVersion() const OVERRIDE;
	virtual TUINT16                GetSubVersion() const OVERRIDE;
	virtual TUINT16                GetBuild() const OVERRIDE;
	virtual const void*            GetDeviceIdentifier() const OVERRIDE;
	virtual TUINT32                GetNumSupportedDevices() const OVERRIDE;

	void EnumerateOutputs( RenderDX11* render, IDXGIAdapter* dxgiAdapter );

	void UpdateAdapterInfo()
	{
		m_AdapterLuid = m_AdapterDesc.AdapterLuid;
		m_VendorId    = m_AdapterDesc.VendorId;
		m_DeviceId    = m_AdapterDesc.DeviceId;
		m_SubSysId    = m_AdapterDesc.SubSysId;
		m_Revision    = m_AdapterDesc.Revision;
	}

	DXGI_ADAPTER_DESC* GetAdapterDesc() { return &m_AdapterDesc; }
	Mode* GetMode() { return &m_Mode; }

	const TCHAR* GetDescription() { return m_Description; }
	void SetDescription( const TWCHAR a_pcDescription[ 128 ] ) { m_Description = a_pcDescription; }

	void SetAdapterIndex( TUINT32 a_uiAdapterIndex ) { m_AdapterIndex = a_uiAdapterIndex; }
	void SetDriver( const Toshi::TString8& a_rDriver ) { m_Driver = a_rDriver; }
	void SetDriverVersionLowPart( DWORD a_uiDriverVersionLowPart ) { m_DriverVersionLowPart = a_uiDriverVersionLowPart; }
	void SetDriverVersionHighPart( DWORD a_uiDriverVersionHighPart ) { m_DriverVersionHighPart = a_uiDriverVersionHighPart; }
	void SetVendorId( DWORD a_uiVendorId ) { m_VendorId = a_uiVendorId; }
	void SetDeviceId( DWORD a_uiDeviceId ) { m_DeviceId = a_uiDeviceId; }
	void SetSubSysId( DWORD a_uiSubSysId ) { m_SubSysId = a_uiSubSysId; }
	void SetRevision( DWORD a_uiRevision ) { m_Revision = a_uiRevision; }

private:
	DXGI_ADAPTER_DESC m_AdapterDesc;
	Mode              m_Mode;
	TUINT32           m_AdapterIndex;
	LUID              m_AdapterLuid;
	Toshi::TString8   m_Driver;
	Toshi::TString8   m_Description;
	DWORD             m_DriverVersionLowPart;
	DWORD             m_DriverVersionHighPart;
	DWORD             m_VendorId;
	DWORD             m_DeviceId;
	DWORD             m_SubSysId;
	DWORD             m_Revision;
};

}; // namespace remaster
