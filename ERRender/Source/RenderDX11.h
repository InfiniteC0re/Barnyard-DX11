#pragma once
#include "UI/FontAtlas.h"

#include <Toshi/TDList.h>
#include <Toshi/T2Pair.h>
#include <Toshi/T2Map.h>
#include <Render/TRenderInterface.h>
#include <Render/TRenderContext.h>
#include <Render/TRenderAdapter.h>
#include <Render/TOrderTable.h>
#include <Platform/DX8/TMSWindow.h>

#include <d3d11.h>
#include <d2d1_3.h>
#include <dwrite_3.h>

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

	static constexpr TSIZE HEAPSIZE                     = 0x10000;
	static constexpr TSIZE VERTEX_CONSTANT_BUFFER_SIZE  = 0x1000;
	static constexpr TSIZE PIXEL_CONSTANT_BUFFER_SIZE   = 0x400;
	static constexpr TSIZE NUMBUFFERS                   = 16;
	static constexpr TSIZE IMMEDIATE_VERTEX_BUFFER_SIZE = 0x100;
	static constexpr TSIZE IMMEDIATE_INDEX_BUFFER_SIZE  = 0x10;

	typedef TUINT8 BlendMode;
	enum BlendMode_ : BlendMode
	{
		BlendMode_Opaque,
		BlendMode_Modulate,
		BlendMode_Additive,
		BlendMode_Subtractive,
		BlendMode_ZPass,
		BlendMode_NoZWrite,
		BlendMode_NoZWriteAlpha,
		BlendMode_Translucent,
		BLENDMODE_NUMOF,
		BLENDMODE_MASK = BLENDMODE_NUMOF - 1,
	};

	union BlendState
	{
		struct
		{
			// m_BlendState1
			D3D11_BLEND_OP BlendOp : 3;
			D3D11_BLEND_OP BlendOpAlpha : 3;
			D3D11_BLEND    SrcBlendAlpha : 5;
			D3D11_BLEND    DestBlendAlpha : 5;
			// m_BlendState2
			TUINT32     RenderTargetWriteMask : 4;
			BOOL        bBlendEnabled : 1;
			D3D11_BLEND SrcBlend : 5;
			D3D11_BLEND DestBlend : 5;
			TUINT32     Unknown2 : 1;
		} Parts;

		TUINT32 Raw;

		operator const TUINT32&() const { return Raw; }
	};

	struct RasterizerId
	{
		union
		{
			struct
			{
				TUINT32 FillMode : 2;               // D3D11_FILL_MODE
				TUINT32 CullMode : 2;               // D3D11_CULL_MODE
				TUINT32 bFrontCounterClockwise : 1; // BOOL
				TUINT32 bDepthClipEnable : 1;       // BOOL
				TUINT32 bScissorEnable : 1;         // BOOL
				TUINT32 bMultisampleEnable : 1;     // BOOL
			} Parts;

			TUINT32 Raw;
		} Flags;

		TINT   DepthBias;
		TFLOAT SlopeScaledDepthBias;

		TBOOL operator==( const RasterizerId& other ) const { return Flags.Raw == other.Flags.Raw && DepthBias == other.DepthBias && SlopeScaledDepthBias == other.SlopeScaledDepthBias; }
		TBOOL operator!=( const RasterizerId& other ) const { return Flags.Raw != other.Flags.Raw || DepthBias != other.DepthBias || SlopeScaledDepthBias != other.SlopeScaledDepthBias; }
		TBOOL operator<( const RasterizerId& other ) const { return Flags.Raw < other.Flags.Raw && DepthBias < other.DepthBias && SlopeScaledDepthBias < other.SlopeScaledDepthBias; }
	};

	struct RasterizerIdComparator
	{
		static TBOOL IsEqual( const RasterizerId& a, const RasterizerId& b ) { return a == b; }
		static TBOOL IsLess( const RasterizerId& a, const RasterizerId& b ) { return a < b; }
	};

	union DepthState
	{
		struct
		{
			TUINT64 bDepthEnable : 1;
			TUINT64 DepthWriteMask : 1;
			TUINT64 DepthFunc : 4;
			TUINT64 bStencilEnable : 1;
			TUINT64 PADDING : 1;
			TUINT64 StencilReadMask : 8;
			TUINT64 StencilWriteMask : 8;
			TUINT64 FrontFaceStencilFailOp : 4;
			TUINT64 FrontFaceStencilDepthFailOp : 4;
			TUINT64 FrontStencilPassOp : 4;
			TUINT64 FrontStencilFunc : 4;
			TUINT64 BackFaceStencilFailOp : 4;
			TUINT64 BackFaceStencilDepthFailOp : 4;
			TUINT64 BackStencilPassOp : 4;
			TUINT64 BackStencilFunc : 4;
		} Parts;

		TUINT64 Raw;

		operator const TUINT64&() const { return Raw; }
	};

	using DepthPair = Toshi::T2Pair<DepthState, TUINT, Toshi::TComparator<TUINT64>>;

	typedef TUINT32 VSBufferOffset;
	enum VSBufferOffset_ : VSBufferOffset
	{

	};

	typedef TUINT32 PSBufferOffset;
	enum PSBufferOffset_ : PSBufferOffset
	{

	};

public:
	RenderDX11();
	~RenderDX11();

	//-----------------------------------------------------------------------------
	// Toshi::TRenderInterface
	//-----------------------------------------------------------------------------
	virtual TBOOL                                CreateDisplay( const DISPLAYPARAMS& a_rParams ) OVERRIDE;
	virtual TBOOL                                DestroyDisplay() OVERRIDE;
	virtual TBOOL                                Update( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual TBOOL                                BeginScene() OVERRIDE;
	virtual TBOOL                                EndScene() OVERRIDE;
	virtual Toshi::TRenderAdapter::Mode::Device* GetCurrentDevice() OVERRIDE;
	virtual DISPLAYPARAMS*                       GetCurrentDisplayParams() OVERRIDE;
	virtual void                                 FlushOrderTables() OVERRIDE;
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

	//-----------------------------------------------------------------------------
	// Own methods (NEEDED to replicate interface of TRenderD3DInterface!!!
	//-----------------------------------------------------------------------------
	virtual TBOOL  RecreateDisplay( const DISPLAYPARAMS& a_rDisplayParams );
	virtual void   SetContrast( TFLOAT a_fConstrast );
	virtual void   SetBrightness( TFLOAT a_fBrightness );
	virtual void   SetGamma( TFLOAT a_fGamma );
	virtual void   SetSaturate( TFLOAT a_fSaturate );
	virtual TFLOAT GetContrast() const;
	virtual TFLOAT GetBrightness() const;
	virtual TFLOAT GetGamma() const;
	virtual TFLOAT GetSaturate() const;
	virtual void   UpdateColourSettings();
	virtual TBOOL  IsCapableColourCorrection();
	virtual void   EnableColourCorrection( TBOOL a_bEnable );
	virtual void   ForceEnableColourCorrection( TBOOL a_bEnable );
	virtual TBOOL  IsColourCorrection();

public:
	TBOOL Create( const TCHAR* a_pchWindowTitle );
	void  CreateRenderObjects();

	ID3D11SamplerState* CreateSamplerState(
	    D3D11_FILTER               filter,
	    D3D11_TEXTURE_ADDRESS_MODE addressU,
	    D3D11_TEXTURE_ADDRESS_MODE addressV,
	    D3D11_TEXTURE_ADDRESS_MODE addressW,
	    TFLOAT                     mipLODBias,
	    TUINT32                    borderColor,
	    TFLOAT                     minLOD,
	    TFLOAT                     maxLOD,
	    TUINT                      maxAnisotropy
	);

	ID3D11SamplerState* CreateSamplerStateAutoAnisotropy(
	    D3D11_FILTER               filter,
	    D3D11_TEXTURE_ADDRESS_MODE addressU,
	    D3D11_TEXTURE_ADDRESS_MODE addressV,
	    D3D11_TEXTURE_ADDRESS_MODE addressW,
	    TFLOAT                     mipLODBias,
	    TUINT32                    borderColor,
	    TFLOAT                     minLOD,
	    TFLOAT                     maxLOD
	);

public:
	//-----------------------------------------------------------------------------
	// Buffers management
	//-----------------------------------------------------------------------------

	void VSBufferSetVec4( VSBufferOffset a_uiOffset, const void* a_pData, TINT a_iCount = 1 );
	void PSBufferSetVec4( PSBufferOffset a_uiOffset, const void* a_pData, TINT a_iCount = 1 );

public:
	//-----------------------------------------------------------------------------
	// Render states management
	//-----------------------------------------------------------------------------
	void SetDstAlpha( TFLOAT a_fAlpha );
	void SetBlendEnabled( TBOOL a_bBlendEnabled ) { m_BlendState.Parts.bBlendEnabled = a_bBlendEnabled; }
	void SetDepthWrite( TBOOL a_bWrite ) { m_DepthState.first.Parts.DepthWriteMask = a_bWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO; }
	void SetBlendMode( TBOOL a_bBlendEnabled, D3D11_BLEND_OP a_eBlendOp, D3D11_BLEND a_eSrcBlendAlpha, D3D11_BLEND a_eDestBlendAlpha );
	void SetAlphaUpdate( TBOOL a_bUpdate );
	void SetColorUpdate( TBOOL a_bUpdate );
	void SetZMode( TBOOL a_bDepthEnable, D3D11_COMPARISON_FUNC a_eComparisonFunc, D3D11_DEPTH_WRITE_MASK a_eDepthWriteMask );
	void SetDepthClip( TBOOL a_bClip );

	D3D11_BLEND_OP GetBlendOp() const { return m_BlendState.Parts.BlendOp; }
	TBOOL          IsBlendEnabled() const { return m_BlendState.Parts.bBlendEnabled; }
	TBOOL          IsZEnabled() const { return m_DepthState.first.Parts.bDepthEnable; }

	void DrawImmediately( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_iIndexCount, const void* a_pIndexData, DXGI_FORMAT a_eFormat, const void* a_pVertexData, TUINT a_iStrideSize, TUINT a_iStrides );
	void DrawIndexed( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_uiIndexCount, ID3D11Buffer* a_pIndexBuffer, TUINT a_uiIndexBufferOffset, DXGI_FORMAT a_eIndexBufferFormat, ID3D11Buffer* a_pVertexBuffer, TUINT a_pStrides, TUINT a_pOffsets );
	void DrawNonIndexed( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveTopology, ID3D11Buffer* a_pVertexBuffer, TUINT a_uiVertexCount, TUINT a_uiStrides, TUINT a_uiStartVertex, TUINT a_uiOffsets );
	void CopyDataToTexture( ID3D11ShaderResourceView* a_pSRTex, TUINT a_uiDataSize, const void* a_pData, TUINT a_uiTextureSize );
	void SetSamplerState( TUINT a_uiStartSlot, TINT a_iSamplerId, BOOL a_bSetForPS );
	void SetCullMode( D3D11_CULL_MODE a_eMode ) { m_RasterizerState.Flags.Parts.CullMode = a_eMode; }
	void WaitForEndOfRender();
	void UpdateRenderStates();
	void FlushConstantBuffers();

	void SetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY a_eCurrentTopology )
	{
		if ( m_eCurrentTopology != a_eCurrentTopology )
		{
			m_eCurrentTopology = a_eCurrentTopology;
			m_pDeviceContext->IASetPrimitiveTopology( a_eCurrentTopology );
		}
	}

	void SetVertexBuffer( ID3D11Buffer* a_pVertexBuffer, TUINT a_uiStride, TUINT a_uiOffset )
	{
		if ( a_pVertexBuffer != m_pCurrentVertexBuffer || a_uiStride != m_uiVBCurrentStride || a_uiOffset != m_uiVBCurrentOffset )
		{
			m_pDeviceContext->IASetVertexBuffers( 0, 1, &a_pVertexBuffer, &a_uiStride, &a_uiOffset );
			m_pCurrentVertexBuffer = a_pVertexBuffer;
			m_uiVBCurrentStride      = a_uiStride;
			m_uiVBCurrentOffset      = a_uiOffset;
		}
	}

	void SetIndexBuffer( ID3D11Buffer* a_pIndexBuffer, DXGI_FORMAT a_eFormat, TUINT a_uiOffset )
	{
		if ( a_pIndexBuffer != m_pCurrentIndexBuffer || a_eFormat != m_eIBCurrentFormat || a_uiOffset != m_uiIBCurrentOffset )
		{
			m_pDeviceContext->IASetIndexBuffer( a_pIndexBuffer, a_eFormat, a_uiOffset );
			m_pCurrentIndexBuffer = a_pIndexBuffer;
			m_eIBCurrentFormat    = a_eFormat;
			m_uiIBCurrentOffset   = a_uiOffset;
		}
	}

	void VSSetConstantBuffer( TINT a_iSlot, ID3D11Buffer* a_pBuffer )
	{
		if ( m_aVSCurrentConstantBuffers[ a_iSlot ] != a_pBuffer )
		{
			m_pDeviceContext->VSSetConstantBuffers( a_iSlot, 1, &a_pBuffer );
			m_aVSCurrentConstantBuffers[ a_iSlot ] = a_pBuffer;
		}
	}

	void PSSetConstantBuffer( TINT a_iSlot, ID3D11Buffer* a_pBuffer )
	{
		if ( m_aPSCurrentConstantBuffers[ a_iSlot ] != a_pBuffer )
		{
			m_pDeviceContext->PSSetConstantBuffers( a_iSlot, 1, &a_pBuffer );
			m_aPSCurrentConstantBuffers[ a_iSlot ] = a_pBuffer;
		}
	}

public:
	//-----------------------------------------------------------------------------
	// Main Getters/Setters
	//-----------------------------------------------------------------------------

	Toshi::TPriList<Toshi::TOrderTable>& GetOrderTables() { return m_OrderTables; }
	ID3D11Device*                        GetD3D11Device() const { return m_pDevice; }
	ID3D11DeviceContext*                 GetD3D11DeviceContext() const { return m_pDeviceContext; }
	IDXGISwapChain*                      GetD3D11SwapChain() const { return m_pSwapChain; }
	ID3D11RenderTargetView*              GetD3D11RenderTargetView() const { return m_pRenderTargetView; }
	ID3D11DepthStencilView*              GetD3D11DepthStencilView() const { return m_pDepthStencilView; }

	ID2D1Factory*      GetD2DFactory() const { return m_pD2DFactory; }
	ID2D1RenderTarget* GetD2DRenderTarget() const { return m_pD2DRenderTarget; }
	IDWriteFactory*    GetDWriteFactory() const { return m_pDWFactory; }
	IDWriteFontFile*   GetDWriteFontFile() const { return m_pDWFontFile; }
	IDWriteFontFace*   GetDWriteFontFace() const { return m_pDWFontFace; }
	const DWRITE_FONT_METRICS& GetFontMetrics() const { return m_oFontMetrics; }

	TFLOAT GetSurfaceWidth() const { return TFLOAT( m_oSwapChainDesc.BufferDesc.Width ); }
	TFLOAT GetSurfaceHeight() const { return TFLOAT( m_oSwapChainDesc.BufferDesc.Height ); }

	FontAtlas* GetFontAtlas() const { return m_pFontAtlas; }

private:
	void BuildAdapterDatabase();

private:
	ID3D11Device*                        m_pDevice        = TNULL; // NOTE: DUE TO COMPATIBILITY, IT NEEDS TO BE AT THIS OFFSET!!!
	ID3D11DeviceContext*                 m_pDeviceContext = TNULL; // NOTE: DUE TO COMPATIBILITY, IT NEEDS TO BE AT THIS OFFSET!!!

	// Things left from TRenderD3DInterface (D3D8)
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

	// D3D11 main objects
	D3D_FEATURE_LEVEL       m_eFeatureLevel;
	IDXGISwapChain*         m_pSwapChain           = TNULL;
	ID3D11RenderTargetView* m_pRenderTargetView    = TNULL;
	ID3D11Texture2D*        m_pRenderTargetTexture = TNULL;
	ID3D11Texture2D*        m_pDepthStencilTexture = TNULL;
	ID3D11DepthStencilView* m_pDepthStencilView    = TNULL;
	DXGI_SWAP_CHAIN_DESC    m_oSwapChainDesc;

	// DirectWrite
	ID2D1RenderTarget*        m_pD2DRenderTarget = TNULL;
	IDWriteFactory*           m_pDWFactory       = TNULL;
	ID2D1Factory*             m_pD2DFactory      = TNULL;
	IDWriteFontFile*          m_pDWFontFile      = TNULL;
	IDWriteFontFace*          m_pDWFontFace      = TNULL;
	ID3D11ShaderResourceView* m_pTextAtlasSRV    = TNULL;
	DWRITE_FONT_METRICS       m_oFontMetrics;
	FontAtlas*                m_pFontAtlas;
	
	// Buffers
	void*         m_pVertexConstantBuffer;
	TBOOL         m_IsVertexConstantBufferSet;
	ID3D11Buffer* m_VertexBuffers[ NUMBUFFERS ];
	TSIZE         m_VertexBufferIndex;
	TSIZE         m_VertexBufferWrittenSize;

	void* m_pPixelConstantBuffer;
	TBOOL m_IsPixelConstantBufferSet;

	ID3D11Buffer* m_PixelBuffers[ NUMBUFFERS ];
	TSIZE         m_PixelBufferIndex;

	ID3D11Buffer* m_MainVertexBuffer;
	TUINT         m_iImmediateVertexCurrentOffset;

	ID3D11Buffer* m_MainIndexBuffer;
	TUINT         m_iImmediateIndexCurrentOffset;

	// Various states
	TFLOAT              m_aClearColor[ 4 ];
	ID3D11SamplerState* m_aSamplerStates[ 12 ];

	// Depth states
	Toshi::T2Map<DepthState, ID3D11DepthStencilState*> m_DepthStatesTree;
	DepthPair                                          m_DepthState;
	DepthPair                                          m_PreviousDepth;

	// Rasterizer states
	Toshi::T2Map<RasterizerId, ID3D11RasterizerState*, RasterizerIdComparator> m_RasterizersTree;
	RasterizerId                                                               m_RasterizerState;
	RasterizerId                                                               m_PreviousRasterizerId;

	// Blend states
	Toshi::T2Map<BlendState, ID3D11BlendState*> m_BlendStatesTree;
	BlendState                                  m_BlendState;
	TFLOAT                                      m_aCurrentBlendFactor[ 4 ];
	BlendState                                  m_PreviousBlendState;
	TFLOAT                                      m_PreviousBlendFactor[ 4 ];

	// Device states
	D3D11_PRIMITIVE_TOPOLOGY m_eCurrentTopology;
	ID3D11Buffer*            m_pCurrentVertexBuffer;
	TUINT                    m_uiVBCurrentStride;
	TUINT                    m_uiVBCurrentOffset;

	ID3D11Buffer* m_pCurrentIndexBuffer;
	DXGI_FORMAT   m_eIBCurrentFormat;
	TUINT         m_uiIBCurrentOffset;

	ID3D11Buffer* m_aVSCurrentConstantBuffers[ 16 ];
	ID3D11Buffer* m_aPSCurrentConstantBuffers[ 16 ];
};

extern RenderDX11* g_pRender;

} // namespace remaster
