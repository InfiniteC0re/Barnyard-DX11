#pragma once
#include <Toshi/TDList.h>
#include <Render/TRenderInterface.h>
#include <Render/TRenderContext.h>
#include <Render/TRenderAdapter.h>
#include <Render/TOrderTable.h>
#include <Platform/DX8/TMSWindow.h>

#include <d3d11.h>

#define DX11_API_VALIDATE( CALL )       \
	{                                   \
		HRESULT hr = CALL;              \
		TASSERT( S_OK == hr && #CALL ); \
	}

#define DX11_API_VALIDATE_EXIT( CALL )   \
	{                                    \
		HRESULT hr = CALL;               \
		TASSERT( S_OK == hr && #CALL );  \
		if ( S_OK != hr ) return TFALSE; \
	}

namespace remaster
{

void SetupRenderHooks();

class RenderDX11 : public Toshi::TRenderInterface
{
public:
	TDECLARE_CLASS( RenderDX11, Toshi::TRenderInterface );

public:
	RenderDX11();
	~RenderDX11();

	virtual TBOOL                                CreateDisplay( const DISPLAYPARAMS& a_rParams ) OVERRIDE;
	virtual TBOOL                                DestroyDisplay() OVERRIDE;
	virtual TBOOL                                Update( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual TBOOL                                BeginScene() OVERRIDE;
	virtual TBOOL                                EndScene() OVERRIDE;
	virtual Toshi::TRenderAdapter::Mode::Device* GetCurrentDevice() OVERRIDE;
	virtual DISPLAYPARAMS*                       GetCurrentDisplayParams() OVERRIDE;
	virtual TBOOL                                Supports32BitTextures() OVERRIDE;
	virtual Toshi::TRenderContext*               CreateRenderContext() OVERRIDE;
	virtual Toshi::TRenderCapture*               CreateCapture() OVERRIDE;
	virtual void                                 DestroyCapture( Toshi::TRenderCapture* a_pRenderCapture ) OVERRIDE;
	virtual void*                                CreateUnknown( const TCHAR* a_szName, TINT a_iUnk1, TINT a_iUnk2, TINT a_iUnk3 ) OVERRIDE;
	virtual Toshi::TModel*                       CreateModelTMD( Toshi::TTMD* a_pTMD, TBOOL a_bLoad ) OVERRIDE;
	virtual Toshi::TModel*                       CreateModelTMDFile( const TCHAR* a_szFilePath, TBOOL a_bLoad ) OVERRIDE;
	virtual Toshi::TModel*                       CreateModelTRB( const TCHAR* a_szFilePath, TBOOL a_bLoad, Toshi::TTRB* a_pAssetTRB, TUINT8 a_ui8FileNameLen ) OVERRIDE;
	virtual Toshi::TDebugText*                   CreateDebugText() OVERRIDE;
	virtual void                                 DestroyDebugText() OVERRIDE;

	virtual TBOOL RecreateDisplay( const DISPLAYPARAMS& a_rDisplayParams );
	virtual void SetContrast( TFLOAT a_fConstrast );
	virtual void SetBrightness( TFLOAT a_fBrightness );
	virtual void SetGamma( TFLOAT a_fGamma );
	virtual void SetSaturate( TFLOAT a_fSaturate );
	virtual TFLOAT GetContrast() const;
	virtual TFLOAT GetBrightness() const;
	virtual TFLOAT GetGamma() const;
	virtual TFLOAT GetSaturate() const;
	virtual void UpdateColourSettings();
	virtual TBOOL IsCapableColourCorrection();
	virtual void EnableColourCorrection( TBOOL a_bEnable );
	virtual void ForceEnableColourCorrection( TBOOL a_bEnable );
	virtual TBOOL IsColourCorrection();

	TBOOL Create( const TCHAR* a_pchWindowTitle );
	void  BuildAdapterDatabase();

	Toshi::TPriList<Toshi::TOrderTable>& GetOrderTables() { return m_OrderTables; }


private:
	IDirect3D8*                          m_pDirect3D;     // Direct3D interface
	IDirect3DDevice8*                    m_pDirectDevice; // Direct3D device
	TBYTE                                PADDING1[ 84 ];
	TFLOAT                               m_fPixelAspectRatio;               // Pixel aspect ratio
	HACCEL                               m_AcceleratorTable;                // Accelerator table
	Toshi::TRenderAdapter::Mode::Device* m_pAdapterDevice;                  // Current device
	DISPLAYPARAMS                        m_oDisplayParams;                  // Display parameters
	Toshi::TMSWindow                     m_Window;                          // Window
	TBOOL                                m_bExited;                         // Exit flag
	TFLOAT                               m_fContrast;                       // Contrast value
	TFLOAT                               m_fBrightness;                     // Brightness value
	TFLOAT                               m_fGamma;                          // Gamma value
	TFLOAT                               m_fSaturate;                       // Saturation value
	TBOOL                                m_bChangedColourSettings;          // Color settings changed flag
	TBOOL                                m_bCheckedCapableColourCorrection; // Color correction capability checked flag
	TBOOL                                m_bCapableColourCorrection;        // Color correction capability flag
	TBOOL                                m_bEnableColourCorrection;         // Color correction enabled flag
	TBYTE                                PADDING2[ 1536 ];
	TBOOL                                m_bFailed;     // Failure flag
	void*                                m_Unk1;        // Unknown 1
	void*                                m_Unk2;        // Unknown 2
	Toshi::TPriList<Toshi::TOrderTable>  m_OrderTables; // Order tables

	// Remaster's additions...
	D3D_FEATURE_LEVEL       m_eFeatureLevel;
	ID3D11Device*           m_pDevice           = TNULL;
	ID3D11DeviceContext*    m_pDeviceContext    = TNULL;
	IDXGISwapChain*         m_pSwapChain        = TNULL;
	ID3D11RenderTargetView* m_pRenderTargetView = TNULL;
};

extern RenderDX11* g_pRender;

class RenderContextD3D11 : public Toshi::TRenderContext
{
public:
	RenderContextD3D11( Toshi::TRenderInterface* a_pRenderer );
	~RenderContextD3D11();

	virtual void Update() override;

	void ComputePerspectiveProjection();
	void ComputeOrthographicProjection();

	void ComputePerspectiveFrustum();
	void ComputeOrthographicFrustum();

	const Toshi::TMatrix44& GetProjectionMatrix() const { return m_Projection; }

private:
	Toshi::TMatrix44 m_Projection;
};

} // namespace remaster
