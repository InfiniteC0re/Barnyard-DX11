#pragma once
#include "UI/FontAtlas.h"
#include "SDLWindow.h"
#include "CSM/CSMManager.h"
#include "LightManager.h"

#include <Toshi/TDList.h>
#include <Toshi/T2Pair.h>
#include <Toshi/T2Map.h>
#include <ToshiTools/T2DynamicVector.h>
#include <Math/TMatrix44.h>
#include <Render/TRenderInterface.h>
#include <Render/TRenderContext.h>
#include <Render/TRenderAdapter.h>
#include <Render/TOrderTable.h>
#include <Platform/DX8/TMSWindow.h>

#include <xmmintrin.h>
#include <d3d11_1.h>

// GPU-side profiling. Real Tracy timestamp queries under --profiler=perf; the
// header's TRACY_ENABLE guard makes every macro/type a no-op otherwise.
#include "Profiler/tracy/TracyD3D11.hpp"

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

// Deferred shader warm-up: boot-critical combos compile in CreateRenderObjects, the rest on a
// worker thread (D3DCompile only) started by Begin. RunStep finalizes ready families on the
// main thread each frame from the SaveLoadSKU::OnUpdate hook that holds the boot state
void  ShaderWarmup_Begin();
TBOOL ShaderWarmup_RunStep();
TBOOL ShaderWarmup_IsComplete();
void  ShaderWarmup_EnsureFinished();
void  ShaderWarmup_GetProgress( TINT& a_riDone, TINT& a_riTotal );

// Procedural warm-up background (ScreenSpace ps_boot_background); drawn from the GUI
// renderer's BeginScene since the GUI viewport clear would wipe anything drawn earlier
void DrawBootBackground();

enum SAMPLERSTATE : TINT
{
	SAMPLER_POINT_CLAMP           = 0,  // point,            clamp / clamp / clamp
	SAMPLER_LINEAR_CLAMP          = 1,  // linear (aniso),   clamp / clamp / clamp
	SAMPLER_POINT_WRAP            = 2,  // point,            wrap  / wrap  / wrap
	SAMPLER_LINEAR_WRAP           = 3,  // linear (aniso),   wrap  / wrap  / wrap
	SAMPLER_LINEAR_MIRROR         = 4,  // linear (aniso),   mirror/ mirror/ mirror
	SAMPLER_BILINEAR_CLAMP        = 5,  // linear/mip-point, clamp / clamp / clamp
	SAMPLER_BILINEAR_WRAP         = 6,  // linear/mip-point, wrap  / wrap  / wrap
	SAMPLER_BILINEAR_WRAP_BIAS    = 7,  // linear/mip-point, wrap  / wrap  / wrap, mip bias -1
	SAMPLER_ANISO_CLAMP           = 8,  // anisotropic,      clamp / clamp / clamp
	SAMPLER_POINT_WRAPU_CLAMPV    = 9,  // point,            wrap  / clamp / wrap
	SAMPLER_LINEAR_WRAPU_CLAMPV   = 10, // linear (aniso),   wrap  / clamp / wrap
	SAMPLER_BILINEAR_WRAPU_CLAMPV = 11, // linear/mip-point, wrap  / clamp / wrap
	SAMPLER_POINT_CLAMPU_WRAPV    = 12, // point,            clamp / wrap  / clamp
	SAMPLER_LINEAR_CLAMPU_WRAPV   = 13, // linear (aniso),   clamp / wrap  / clamp
	SAMPLER_BILINEAR_CLAMPU_WRAPV = 14, // linear/mip-point, clamp / wrap  / clamp
	SAMPLER_BILINEAR_MIRROR       = 15, // linear/mip-point, mirror/ mirror/ mirror

	SAMPLER_COUNT,
};

// Selects the linear (anisotropic) sampler matching a texture's U/V addressing.
TINT GetLinearSamplerForAddressing( Toshi::ADDRESSINGMODE a_eAddressU, Toshi::ADDRESSINGMODE a_eAddressV );

// Slots of the shared per-pass constant buffer (b4, VS+PS); layout mirrors PerPassCB in Resources\Shaders\PerPass.hlsli
typedef TUINT32 PassBufferOffset;
enum PassBufferOffset_ : PassBufferOffset
{
	PASSBUF_CAMERA_POS,     // xyz = camera world position
	PASSBUF_SUN_DIRECTION,  // xyz = direction toward the sun (world)
	PASSBUF_FOG_COLOR,      // xyz = fog colour, w = density
	PASSBUF_FOG_PARAMS,     // x = fog start, y = fog end
	PASSBUF_WIND_PARAMS,    // xy = wind dir (world XZ), z = strength, w = time
	PASSBUF_ENV_SPECULAR,   // x = global env intensity, y = cube max mip
	PASSBUF_ENV_PARALLAX,   // xyz = "to" box half-extents
	PASSBUF_ENV_PROBE_POS,  // xyz = "to" probe centre, w = blend
	PASSBUF_ENV_PARALLAX2,  // xyz = "from" box half-extents
	PASSBUF_ENV_PROBE_POS2, // xyz = "from" probe centre
	PASSBUF_AMBIENT_COLOR,  // world ambient colour
	PASSBUF_SHADOW_COLOR,   // world shadow colour
	PASSBUF_VIEWPROJ,       // 4 slots: world -> clip matrix (see UpdatePassViewProj)
	PASSBUF_NUMOF = PASSBUF_VIEWPROJ + 4,
};

//-----------------------------------------------------------------------------
// Runtime graphics settings
//
// Callers mutate a pending GraphicsSettings copy through RenderDX11::Request*,
// which records a dirty bit. ApplyGraphicsSettings() (run once per frame from
// Update(), outside any scene) consumes the dirty mask and rebuilds only what
// changed: swapchain resize, MSAA/RT recreate, CSM atlas resize, etc.
//-----------------------------------------------------------------------------
enum GFXDirtyFlags : TUINT
{
	GFX_DIRTY_NONE        = 0,
	GFX_DIRTY_RESOLUTION  = 1 << 0, // width/height change -> ResizeBuffers + rebuild sized RTs
	GFX_DIRTY_DISPLAYMODE = 1 << 1, // windowed / borderless / fullscreen
	GFX_DIRTY_VSYNC       = 1 << 2, // Present sync interval
	GFX_DIRTY_MSAA        = 1 << 3, // offscreen MSAA sample count -> rebuild sized RTs
	GFX_DIRTY_CSM         = 1 << 4, // CSM shadow-map resolution preset
	GFX_DIRTY_AA          = 1 << 5, // post-process AA mode (no rebuild; shader-path swap)
};

enum DisplayMode : TUINT
{
	DISPLAY_WINDOWED,
	DISPLAY_BORDERLESS,
	DISPLAY_FULLSCREEN,
};

// Post-process anti-aliasing applied to the tone-mapped LDR image, independent of MSAA
enum AAMode : TUINT
{
	AA_NONE, // no post-process AA
	AA_FXAA, // FXAA 3.11
	AA_SMAA, // SMAA 1x
};

struct GraphicsSettings
{
	TUINT       uiWidth       = 0;
	TUINT       uiHeight      = 0;
	DisplayMode eDisplayMode  = DISPLAY_WINDOWED;
	TBOOL       bVSync        = TFALSE;
	TUINT       uiMSAASamples = 1;                 // 1/2/4/8 desired (clamped to device support)
	CSMPreset   eCSMPreset    = CSM_PRESET_MEDIUM;
	AAMode      eAAMode       = AA_NONE;           // post-process AA (FXAA/SMAA)
};

class RenderDX11 : public Toshi::TRenderInterface
{
public:
	TDECLARE_CLASS( RenderDX11, Toshi::TRenderInterface );

	static constexpr TUINT MSAA_SAMPLE_COUNT            = 4;
	static constexpr TSIZE HEAPSIZE                     = 0x10000;
	static constexpr TSIZE VERTEX_CONSTANT_BUFFER_SIZE  = 192; // 12 vec4 slots (Skin's per-draw max), bound to VS/PS b0 per draw
	static constexpr TSIZE PASS_CONSTANT_BUFFER_SIZE    = PASSBUF_NUMOF * 16; // bound to VS/PS b4
	static constexpr TSIZE SHADOW_CONSTANT_BUFFER_SIZE  = sizeof( ShadowCBufferData );
	static constexpr TSIZE NUMBUFFERS                   = 1;
	static constexpr TSIZE IMMEDIATE_VERTEX_BUFFER_SIZE = 0x4000;
	static constexpr TSIZE IMMEDIATE_INDEX_BUFFER_SIZE  = 0x1000;

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
			TUINT32     bAlphaToCoverage : 1;
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
		TBOOL  operator!=( const RasterizerId& other ) const { return Flags.Raw != other.Flags.Raw || DepthBias != other.DepthBias || SlopeScaledDepthBias != other.SlopeScaledDepthBias; }
	};

	struct RasterizerIdComparator
	{
		TINT operator()( const RasterizerId& a, const RasterizerId& b )
		{
			if ( a.Flags.Raw < b.Flags.Raw && a.DepthBias < b.DepthBias && a.SlopeScaledDepthBias < b.SlopeScaledDepthBias )
				return 1;

			if ( a.Flags.Raw == b.Flags.Raw && a.DepthBias == b.DepthBias && a.SlopeScaledDepthBias == b.SlopeScaledDepthBias )
				return 0;

			return -1;
		}
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

	struct DepthStateComparator
	{
		TUINT64 operator()( const DepthState& a, const DepthState& b )
		{
			return a.Raw - b.Raw;
		}
	};

	using DepthPair = Toshi::T2Pair<DepthState, TUINT, Toshi::TComparator<TUINT64>>;

	typedef TUINT32 VSBufferOffset;
	enum VSBufferOffset_ : VSBufferOffset
	{

	};

	enum FONT
	{
		FONT_REKORD26,
		FONT_REKORD18,
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
	void  CreateRenderTargets();
	void  ReleaseRenderTargets(); // tears down everything CreateRenderTargets allocated

	// Swapchain-size-dependent resources (MSAA colour/glow/G-buffer, depth, back-buffer ref).
	// Split out of CreateDisplay so a runtime resolution/MSAA change can recreate them.
	void CreateSwapchainSizedResources();
	void ReleaseSwapchainSizedResources();

	//-----------------------------------------------------------------------------
	// Runtime graphics settings
	//-----------------------------------------------------------------------------
	const GraphicsSettings& GetGraphicsSettings() const { return m_oActiveSettings; }

	void RequestResolution( TUINT a_uiWidth, TUINT a_uiHeight );
	void RequestDisplayMode( DisplayMode a_eMode );
	void RequestVSync( TBOOL a_bEnabled );
	void RequestMSAA( TUINT a_uiSamples );
	void RequestCSMPreset( CSMPreset a_ePreset );
	void RequestAAMode( AAMode a_eMode );

	// Consumes m_uiGraphicsDirty and applies the pending settings. Must be called
	// between frames (outside BeginScene/EndScene); see Update().
	void ApplyGraphicsSettings();

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

	// Returns the highest MSAA sample count <= a_uiDesired that the device supports
	// for both the colour and depth-stencil formats (falls back to 1 if none).
	TUINT GetSupportedMSAASampleCount( TUINT a_uiDesired ) const;

	TUINT GetMSAASampleCount() const { return m_uiMSAASampleCount; }

public:
	//-----------------------------------------------------------------------------
	// Buffers management
	//-----------------------------------------------------------------------------
	void VSBufferSetVec4( VSBufferOffset a_uiOffset, __m128 a_vData );

	void VSBufferSetMat4( VSBufferOffset a_uiOffset, const Toshi::TMatrix44& a_rData )
	{
		VSBufferSetVec4( a_uiOffset + 0, _mm_load_ps( &a_rData.m_f11 ) );
		VSBufferSetVec4( a_uiOffset + 1, _mm_load_ps( &a_rData.m_f21 ) );
		VSBufferSetVec4( a_uiOffset + 2, _mm_load_ps( &a_rData.m_f31 ) );
		VSBufferSetVec4( a_uiOffset + 3, _mm_load_ps( &a_rData.m_f41 ) );
	}

	// Per-pass (b4) constants: compare-and-set into the CPU shadow copy; uploaded by FlushConstantBuffers only when a value changed
	void PassBufferSetVec4( PassBufferOffset a_uiOffset, __m128 a_vData );

	void PassBufferSetMat4( PassBufferOffset a_uiOffset, const Toshi::TMatrix44& a_rData )
	{
		PassBufferSetVec4( a_uiOffset + 0, _mm_load_ps( &a_rData.m_f11 ) );
		PassBufferSetVec4( a_uiOffset + 1, _mm_load_ps( &a_rData.m_f21 ) );
		PassBufferSetVec4( a_uiOffset + 2, _mm_load_ps( &a_rData.m_f31 ) );
		PassBufferSetVec4( a_uiOffset + 3, _mm_load_ps( &a_rData.m_f41 ) );
	}

	// Refresh the per-pass world->clip matrix (PASSBUF_VIEWPROJ); only redoes the 4x4 multiply when an input changed
	void UpdatePassViewProj( const Toshi::TMatrix44& a_rProjection, const Toshi::TMatrix44& a_rWorldView );

	void VSBufferSetVec4( VSBufferOffset a_uiOffset, const Toshi::TVector4& a_rData ) { VSBufferSetVec4( a_uiOffset, _mm_load_ps( &a_rData.x ) ); }
	void PassBufferSetVec4( PassBufferOffset a_uiOffset, const Toshi::TVector4& a_rData ) { PassBufferSetVec4( a_uiOffset, _mm_load_ps( &a_rData.x ) ); }

public:
	//-----------------------------------------------------------------------------
	// Render states management
	//-----------------------------------------------------------------------------
	void SetDstAlpha( TFLOAT a_fAlpha );
	void SetBlendEnabled( TBOOL a_bBlendEnabled ) { m_BlendState.Parts.bBlendEnabled = a_bBlendEnabled; }
	void SetDepthEnabled( TBOOL a_bDepthEnabled ) { m_DepthState.first.Parts.bDepthEnable = a_bDepthEnabled; }
	void SetDepthWrite( TBOOL a_bWrite ) { m_DepthState.first.Parts.DepthWriteMask = a_bWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO; }
	void SetBlendMode( TBOOL a_bBlendEnabled, D3D11_BLEND_OP a_eBlendOp, D3D11_BLEND a_eSrcBlendAlpha, D3D11_BLEND a_eDestBlendAlpha );
	void SetAlphaUpdate( TBOOL a_bUpdate );
	void SetColorUpdate( TBOOL a_bUpdate );
	void SetZMode( TBOOL a_bDepthEnable, D3D11_COMPARISON_FUNC a_eComparisonFunc, D3D11_DEPTH_WRITE_MASK a_eDepthWriteMask );
	void SetDepthClip( TBOOL a_bClip );
	void SetDepthBias( TINT a_iDepthBias );
	void SetSlopeScaledDepthBias( TFLOAT a_fDepthBias );
	void SetAlphaToCoverageEnabled( TBOOL a_bEnabled ) { m_BlendState.Parts.bAlphaToCoverage = TFALSE; }

	D3D11_BLEND_OP        GetBlendOp() const { return m_BlendState.Parts.BlendOp; }
	TBOOL                 IsBlendEnabled() const { return m_BlendState.Parts.bBlendEnabled; }
	TBOOL                 IsDepthEnabled() const { return m_DepthState.first.Parts.bDepthEnable; }
	D3D11_COMPARISON_FUNC GetDepthFunc() const { return TCAST( D3D11_COMPARISON_FUNC, m_DepthState.first.Parts.DepthFunc ); }

	void DrawImmediately( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_iIndexCount, const void* a_pIndexData, DXGI_FORMAT a_eFormat, const void* a_pVertexData, TUINT a_iStrideSize, TUINT a_iStrides );
	void DrawScreenRectangle();
	void DrawScreenRectangle( ID3D11PixelShader* a_pPixelShader );
	void DrawScreenRectangle( TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight );
	void DrawScreenRectangle( ID3D11PixelShader* a_pPixelShader, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight );
	void DrawIndexed( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_uiIndexCount, ID3D11Buffer* a_pIndexBuffer, TUINT a_uiIndexBufferOffset, DXGI_FORMAT a_eIndexBufferFormat, ID3D11Buffer* a_pVertexBuffer, TUINT a_pStrides, TUINT a_pOffsets, ID3D11Buffer* a_pConstantBuffer );
	void DrawIndexedInstanced( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveType, TUINT a_uiIndexCount, TUINT a_uiInstanceCount, ID3D11Buffer* a_pIndexBuffer, TUINT a_uiIndexBufferOffset, DXGI_FORMAT a_eIndexBufferFormat, ID3D11Buffer* a_pVertexBuffer, TUINT a_pStrides, TUINT a_pOffsets, ID3D11Buffer* a_pConstantBuffer, TUINT a_uiStartInstanceLocation = 0 );
	void DrawNonIndexed( D3D11_PRIMITIVE_TOPOLOGY a_ePrimitiveTopology, ID3D11Buffer* a_pVertexBuffer, TUINT a_uiVertexCount, TUINT a_uiStrides, TUINT a_uiStartVertex, TUINT a_uiOffsets, ID3D11Buffer* a_pConstantBuffer );
	void CopyDataToTexture( ID3D11ShaderResourceView* a_pSRTex, TUINT a_uiDataSize, const void* a_pData, TUINT a_uiTextureSize );
	void SetCullMode( D3D11_CULL_MODE a_eMode ) { m_RasterizerState.Flags.Parts.CullMode = a_eMode; }
	void WaitForEndOfRender();
	void UpdateRenderStates();
	void FlushConstantBuffers();
	void UpdateShadowCBuffer( const ShadowCBufferData& a_rData );

	void FlushShaders();

	void ClearStateCache()
	{
		m_pCurrentRenderTargetView       = decltype( m_pCurrentRenderTargetView )( ~TUINT( m_pCurrentRenderTargetView ) );
		m_pCurrentDepthStencilView       = decltype( m_pCurrentDepthStencilView )( ~TUINT( m_pCurrentDepthStencilView ) );
		m_eCurrentTopology               = decltype( m_eCurrentTopology )( ~TUINT( m_eCurrentTopology ) );
		m_pCurrentVertexBuffer[ 0 ]      = (ID3D11Buffer*)( ~TUINT( m_pCurrentVertexBuffer[ 0 ] ) );
		m_pCurrentVertexBuffer[ 1 ]      = (ID3D11Buffer*)( ~TUINT( m_pCurrentVertexBuffer[ 1 ] ) );
		m_uiVBCurrentStride[ 0 ]         = (TUINT)( ~TUINT( m_uiVBCurrentStride[ 0 ] ) );
		m_uiVBCurrentStride[ 1 ]         = (TUINT)( ~TUINT( m_uiVBCurrentStride[ 1 ] ) );
		m_uiVBCurrentOffset[ 0 ]         = (TUINT)( ~TUINT( m_uiVBCurrentOffset[ 0 ] ) );
		m_uiVBCurrentOffset[ 1 ]         = (TUINT)( ~TUINT( m_uiVBCurrentOffset[ 1 ] ) );
		m_pCurrentIndexBuffer            = decltype( m_pCurrentIndexBuffer )( ~TUINT( m_pCurrentIndexBuffer ) );
		m_eIBCurrentFormat               = decltype( m_eIBCurrentFormat )( ~TUINT( m_eIBCurrentFormat ) );
		m_uiIBCurrentOffset              = decltype( m_uiIBCurrentOffset )( ~TUINT( m_uiIBCurrentOffset ) );
		m_PreviousRasterizerId.Flags.Raw = ~m_RasterizerState.Flags.Raw;
		m_PreviousDepth.first.Raw        = ~m_DepthState.first.Raw;
		m_PreviousBlendState.Raw         = ~m_BlendState.Raw;
	}

	// Full invalidation of every cached device binding. Call after
	// m_pDeviceContext->ClearState() (e.g. the resolution/MSAA rebuild), which unbinds
	// everything on the device. Without this, a re-bind of an already-cached object is
	// wrongly skipped (e.g. the skin bone cbuffer never rebinds and animated meshes
	// collapse to the origin). ClearState leaves NULL bindings, so resetting the caches
	// to TNULL keeps them consistent.
	void InvalidateStateCache()
	{
		ClearStateCache();

		for ( TINT i = 0; i < TARRAYSIZE( m_aVSCurrentConstantBuffers ); i++ )
		{
			m_aVSCurrentConstantBuffers[ i ] = TNULL;
			m_aVSCurrentCBFirst[ i ]         = 0;
			m_aVSCurrentCBNum[ i ]           = 0;
		}
		for ( TINT i = 0; i < TARRAYSIZE( m_aPSCurrentConstantBuffers ); i++ )
		{
			m_aPSCurrentConstantBuffers[ i ] = TNULL;
			m_aPSCurrentCBFirst[ i ]         = 0;
			m_aPSCurrentCBNum[ i ]           = 0;
		}
		for ( TINT i = 0; i < TARRAYSIZE( m_aVSCurrentSampleStates ); i++ )
			m_aVSCurrentSampleStates[ i ] = TNULL;
		for ( TINT i = 0; i < TARRAYSIZE( m_aPSCurrentSampleStates ); i++ )
			m_aPSCurrentSampleStates[ i ] = TNULL;
		for ( TINT i = 0; i < TARRAYSIZE( m_apShaderResourceViewsPS ); i++ )
			m_apShaderResourceViewsPS[ i ] = TNULL;
		for ( TINT i = 0; i < TARRAYSIZE( m_apShaderResourceViewsVS ); i++ )
			m_apShaderResourceViewsVS[ i ] = TNULL;

		m_pCurrentVertexShader = TNULL;
		m_pCurrentPixelShader  = TNULL;
		m_pCurrentInputLayout  = TNULL;
	}

	void SetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY a_eCurrentTopology )
	{
		if ( m_eCurrentTopology != a_eCurrentTopology )
		{
			m_eCurrentTopology = a_eCurrentTopology;
			m_pDeviceContext->IASetPrimitiveTopology( a_eCurrentTopology );
		}
	}

	void SetVertexBuffer( ID3D11Buffer* a_pVertexBuffer, TUINT a_uiStride, TUINT a_uiOffset, TUINT a_uiSlot = 0 )
	{
		if ( a_pVertexBuffer != m_pCurrentVertexBuffer[ a_uiSlot ] || a_uiStride != m_uiVBCurrentStride[ a_uiSlot ] || a_uiOffset != m_uiVBCurrentOffset[ a_uiSlot ] )
		{
			m_pDeviceContext->IASetVertexBuffers( a_uiSlot, 1, &a_pVertexBuffer, &a_uiStride, &a_uiOffset );
			m_pCurrentVertexBuffer[ a_uiSlot ] = a_pVertexBuffer;
			m_uiVBCurrentStride[ a_uiSlot ]    = a_uiStride;
			m_uiVBCurrentOffset[ a_uiSlot ]    = a_uiOffset;
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

	// Whole-buffer binds reset the cached range to the 0/0 sentinel so a later ranged bind of the same buffer isn't wrongly skipped by the cache
	void VSSetConstantBuffer( TINT a_iSlot, ID3D11Buffer* a_pBuffer )
	{
		TASSERT( a_iSlot < TARRAYSIZE( m_aVSCurrentConstantBuffers ) );

		if ( m_aVSCurrentConstantBuffers[ a_iSlot ] != a_pBuffer || m_aVSCurrentCBNum[ a_iSlot ] != 0 )
		{
			m_pDeviceContext->VSSetConstantBuffers( a_iSlot, 1, &a_pBuffer );
			m_aVSCurrentConstantBuffers[ a_iSlot ] = a_pBuffer;
			m_aVSCurrentCBFirst[ a_iSlot ]         = 0;
			m_aVSCurrentCBNum[ a_iSlot ]           = 0;
		}
	}

	void PSSetConstantBuffer( TINT a_iSlot, ID3D11Buffer* a_pBuffer )
	{
		TASSERT( a_iSlot < TARRAYSIZE( m_aPSCurrentConstantBuffers ) );

		if ( m_aPSCurrentConstantBuffers[ a_iSlot ] != a_pBuffer || m_aPSCurrentCBNum[ a_iSlot ] != 0 )
		{
			m_pDeviceContext->PSSetConstantBuffers( a_iSlot, 1, &a_pBuffer );
			m_aPSCurrentConstantBuffers[ a_iSlot ] = a_pBuffer;
			m_aPSCurrentCBFirst[ a_iSlot ]         = 0;
			m_aPSCurrentCBNum[ a_iSlot ]           = 0;
		}
	}

	// Ranged binds via *SetConstantBuffers1: first/count are 16-byte constants, must be multiples of 16 (256-byte granularity); needs the 11.1 context
	void VSSetConstantBufferRange( TINT a_iSlot, ID3D11Buffer* a_pBuffer, TUINT a_uiFirstConstant, TUINT a_uiNumConstants )
	{
		TASSERT( a_iSlot < TARRAYSIZE( m_aVSCurrentConstantBuffers ) );

		if ( m_aVSCurrentConstantBuffers[ a_iSlot ] != a_pBuffer || m_aVSCurrentCBFirst[ a_iSlot ] != a_uiFirstConstant || m_aVSCurrentCBNum[ a_iSlot ] != a_uiNumConstants )
		{
			m_pDeviceContext1->VSSetConstantBuffers1( a_iSlot, 1, &a_pBuffer, &a_uiFirstConstant, &a_uiNumConstants );
			m_aVSCurrentConstantBuffers[ a_iSlot ] = a_pBuffer;
			m_aVSCurrentCBFirst[ a_iSlot ]         = a_uiFirstConstant;
			m_aVSCurrentCBNum[ a_iSlot ]           = a_uiNumConstants;
		}
	}

	void PSSetConstantBufferRange( TINT a_iSlot, ID3D11Buffer* a_pBuffer, TUINT a_uiFirstConstant, TUINT a_uiNumConstants )
	{
		TASSERT( a_iSlot < TARRAYSIZE( m_aPSCurrentConstantBuffers ) );

		if ( m_aPSCurrentConstantBuffers[ a_iSlot ] != a_pBuffer || m_aPSCurrentCBFirst[ a_iSlot ] != a_uiFirstConstant || m_aPSCurrentCBNum[ a_iSlot ] != a_uiNumConstants )
		{
			m_pDeviceContext1->PSSetConstantBuffers1( a_iSlot, 1, &a_pBuffer, &a_uiFirstConstant, &a_uiNumConstants );
			m_aPSCurrentConstantBuffers[ a_iSlot ] = a_pBuffer;
			m_aPSCurrentCBFirst[ a_iSlot ]         = a_uiFirstConstant;
			m_aPSCurrentCBNum[ a_iSlot ]           = a_uiNumConstants;
		}
	}

	void GetRenderTargetView( ID3D11RenderTargetView*& a_pRenderTargetView, ID3D11DepthStencilView*& a_pDepthStencilView )
	{
		a_pRenderTargetView = m_pCurrentRenderTargetView;
		a_pDepthStencilView = m_pCurrentDepthStencilView;
	}

	void SetRenderTargetView( ID3D11RenderTargetView* a_pRenderTargetView, ID3D11DepthStencilView* a_pDepthStencilView )
	{
		if ( m_pCurrentRenderTargetView != a_pRenderTargetView || m_pCurrentDepthStencilView != a_pDepthStencilView || m_bRenderTargetsDirty )
		{
			m_pCurrentRenderTargetView = a_pRenderTargetView;
			m_pCurrentDepthStencilView = a_pDepthStencilView;
			m_bRenderTargetsDirty      = TFALSE;

			// When a secondary (G-buffer) RTV is active it stays bound on slot 1
			// across colour-target swaps (e.g. the glow path) for the whole main pass.
			if ( m_pSecondaryRenderTargetView )
			{
				ID3D11RenderTargetView* apRTVs[ 2 ] = { a_pRenderTargetView, m_pSecondaryRenderTargetView };
				m_pDeviceContext->OMSetRenderTargets( 2, apRTVs, a_pDepthStencilView );
			}
			else
			{
				m_pDeviceContext->OMSetRenderTargets( 1, &a_pRenderTargetView, a_pDepthStencilView );
			}
		}
	}

	// Persistent slot-1 render target for the main-pass G-buffer. Pass TNULL to
	// detach. Rebinds immediately so the slot is (un)bound now.
	void SetSecondaryRenderTargetView( ID3D11RenderTargetView* a_pRenderTargetView )
	{
		if ( m_pSecondaryRenderTargetView == a_pRenderTargetView )
			return;

		// Don't rebind here: m_pCurrent* may be an invalidated sentinel (ClearStateCache
		// sets them to ~ptr). The caller issues a SetRenderTargetView with valid targets
		// right after; m_bRenderTargetsDirty forces it to apply the new slot-1 binding.
		m_pSecondaryRenderTargetView = a_pRenderTargetView;
		m_bRenderTargetsDirty        = TTRUE;
	}

	void VSSetSamplerState( TUINT a_uiStartSlot, TINT a_iSamplerId )
	{
		auto pSampler = m_aSamplerStates[ a_iSamplerId ];

		if ( m_aVSCurrentSampleStates[ a_uiStartSlot ] != pSampler )
		{
			m_pDeviceContext->VSSetSamplers( a_uiStartSlot, 1, &pSampler );
			m_aVSCurrentSampleStates[ a_uiStartSlot ] = pSampler;
		}
	}

	void PSSetSamplerState( TUINT a_uiStartSlot, TINT a_iSamplerId )
	{
		auto pSampler = m_aSamplerStates[ a_iSamplerId ];

		if ( m_aPSCurrentSampleStates[ a_uiStartSlot ] != pSampler )
		{
			m_pDeviceContext->PSSetSamplers( a_uiStartSlot, 1, &pSampler );
			m_aPSCurrentSampleStates[ a_uiStartSlot ] = pSampler;
		}
	}

	void PSSetSamplerState( TUINT a_uiStartSlot, ID3D11SamplerState* a_pSampler )
	{
		TASSERT( a_uiStartSlot < TARRAYSIZE( m_aPSCurrentSampleStates ) );

		if ( m_aPSCurrentSampleStates[ a_uiStartSlot ] != a_pSampler )
		{
			m_pDeviceContext->PSSetSamplers( a_uiStartSlot, 1, &a_pSampler );
			m_aPSCurrentSampleStates[ a_uiStartSlot ] = a_pSampler;
		}
	}

	void ClearRenderTarget( ID3D11RenderTargetView* a_pRenderTargetView, const TFLOAT a_pColorRGBA[ 4 ] )
	{
		m_pDeviceContext->ClearRenderTargetView( a_pRenderTargetView, a_pColorRGBA );
	}

	void DiscardView( ID3D11View* a_pView )
	{
		if ( m_pDeviceContext1 )
			m_pDeviceContext1->DiscardView( a_pView );
	}

	void ClearCurrentRenderTarget( const TFLOAT a_pColorRGBA[ 4 ] )
	{
		m_pDeviceContext->ClearRenderTargetView( m_pCurrentRenderTargetView, a_pColorRGBA );
	}

	struct ShaderPipelineState
	{
		ID3D11VertexShader** ppVertexShader;
		ID3D11PixelShader**  ppPixelShader;
		ID3D11InputLayout*   pInputLayout;

		ID3D11VertexShader* GetVertexShader() const { return ppVertexShader ? *ppVertexShader : TNULL; }
		ID3D11PixelShader*  GetPixelShader() const { return ppPixelShader ? *ppPixelShader : TNULL; }
		void                SetName( const TCHAR* a_pchName );
	};

	void GetCurrentShaderPipelineState( ShaderPipelineState& a_rOutState ) const
	{
		m_pCurrentPipelineVertexShaderSlot = m_pCurrentVertexShader;
		m_pCurrentPipelinePixelShaderSlot  = m_pCurrentPixelShader;
		a_rOutState.ppVertexShader         = &m_pCurrentPipelineVertexShaderSlot;
		a_rOutState.ppPixelShader          = &m_pCurrentPipelinePixelShaderSlot;
		a_rOutState.pInputLayout           = m_pCurrentInputLayout;
	}

	void SetShaderPipelineState( const ShaderPipelineState& a_rPipelineState )
	{
		SetInputLayout( a_rPipelineState.pInputLayout );
		SetVertexShader( a_rPipelineState.GetVertexShader() );
		SetPixelShader( a_rPipelineState.GetPixelShader() );
	}

	ID3D11VertexShader* GetVertexShader() const { return m_pCurrentVertexShader; }
	ID3D11PixelShader*  GetPixelShader() const { return m_pCurrentPixelShader; }
	ID3D11InputLayout*  GetInputLayout() const { return m_pCurrentInputLayout; }

	void SetVertexShader( ID3D11VertexShader* a_pVertexShader )
	{
		if ( m_pCurrentVertexShader != a_pVertexShader )
		{
			m_pCurrentVertexShader = a_pVertexShader;
			m_pDeviceContext->VSSetShader( a_pVertexShader, TNULL, 0 );
		}
	}

	void SetPixelShader( ID3D11PixelShader* a_pPixelShader )
	{
		if ( m_pCurrentPixelShader != a_pPixelShader )
		{
			m_pCurrentPixelShader = a_pPixelShader;
			m_pDeviceContext->PSSetShader( a_pPixelShader, TNULL, 0 );
		}
	}

	void SetInputLayout( ID3D11InputLayout* a_pInputLayout )
	{
		if ( m_pCurrentInputLayout != a_pInputLayout )
		{
			m_pCurrentInputLayout = a_pInputLayout;
			m_pDeviceContext->IASetInputLayout( a_pInputLayout );
		}
	}

	ID3D11ShaderResourceView* PSGetShaderResource( TUINT a_uiSlot ) const
	{
		TASSERT( a_uiSlot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT );
		return m_apShaderResourceViewsPS[ a_uiSlot ];
	}

	void PSSetShaderResource( TUINT a_uiSlot, ID3D11ShaderResourceView* a_pResourceView )
	{
		if ( m_apShaderResourceViewsPS[ a_uiSlot ] != a_pResourceView )
		{
			m_apShaderResourceViewsPS[ a_uiSlot ] = a_pResourceView;
			m_pDeviceContext->PSSetShaderResources( a_uiSlot, 1, &a_pResourceView );
		}
	}

	void VSSetShaderResource( TUINT a_uiSlot, ID3D11ShaderResourceView* a_pResourceView )
	{
		if ( m_apShaderResourceViewsVS[ a_uiSlot ] != a_pResourceView )
		{
			m_apShaderResourceViewsVS[ a_uiSlot ] = a_pResourceView;
			m_pDeviceContext->VSSetShaderResources( a_uiSlot, 1, &a_pResourceView );
		}
	}

public:
	//-----------------------------------------------------------------------------
	// Main Getters/Setters
	//-----------------------------------------------------------------------------

	Toshi::TPriList<Toshi::TOrderTable>& GetOrderTables() { return m_OrderTables; }
	ID3D11Device*                        GetD3D11Device() const { return m_pDevice; }
	ID3D11DeviceContext*                 GetD3D11DeviceContext() const { return m_pDeviceContext; }
	TracyD3D11Ctx                        GetTracyGpuContext() const { return m_pTracyD3D11Ctx; }
	IDXGISwapChain*                      GetD3D11SwapChain() const { return m_pSwapChain; }
	ID3D11RenderTargetView*              GetD3D11RenderTargetView() const { return m_pRenderTargetView; }
	ID3D11ShaderResourceView*            GetD3D11RenderTargetSRV() const { return m_pRenderTargetSRV; }
	ID3D11Texture2D*                     GetD3D11RenderTargetTexture() const { return m_pRenderTargetTexture; }
	ID3D11RenderTargetView*              GetD3D11GlowRenderTargetView() const { return m_pGlowRenderTargetView; }
	ID3D11ShaderResourceView*            GetD3D11GlowRenderTargetSRV() const { return m_pGlowRenderTargetSRV; }
	ID3D11Texture2D*                     GetD3D11GlowRenderTargetTexture() const { return m_pGlowRenderTargetTexture; }
	ID3D11RenderTargetView*              GetD3D11GBufferRTV() const { return m_pGBufferRTV; }
	ID3D11Texture2D*                     GetD3D11GBufferTexture() const { return m_pGBufferTexture; }
	ID3D11DepthStencilView*              GetD3D11DepthStencilView() const { return m_pDepthStencilView; }
	ID3D11ShaderResourceView*            GetD3D11DepthStencilSRV() const { return m_pDepthStencilSRV; }
	ID3D11Buffer*                        GetShadowConstantBuffer() const { return m_pShadowConstantBuffer; }
	ID3D11Buffer*                        GetDepthPassConstantBuffer() const { return m_pDepthPassConstantBuffer; }
	ID3D11PixelShader*                   GetRedTintPixelShader() const { return m_pRedTintPixelShader; }

	const DXGI_SWAP_CHAIN_DESC* GetSwapChainDesc() const { return &m_oSwapChainDesc; }

	TFLOAT GetSurfaceWidth() const { return TFLOAT( m_oSwapChainDesc.BufferDesc.Width ); }
	TFLOAT GetSurfaceHeight() const { return TFLOAT( m_oSwapChainDesc.BufferDesc.Height ); }

	struct Resolution
	{
		TUINT uiWidth;
		TUINT uiHeight;

		TBOOL operator==( const Resolution& a_rc ) const { return uiWidth == a_rc.uiWidth && uiHeight == a_rc.uiHeight; }
	};

	const Toshi::T2DynamicVector<Resolution>& GetAvailableResolutions() const { return m_vecAvailableResolutions; }

	FontAtlas* GetFontAtlas( FONT a_eFontIndex ) const { return m_pFontAtlases[ a_eFontIndex ]; }

	CSMManager&   GetCSMManager() { return m_oCSMManager; }
	LightManager& GetLightManager() { return m_oLightManager; }

	// TRUE when *SetConstantBuffers1 offset binding is usable (11.1 context + driver support)
	TBOOL IsCBOffsettingSupported() const { return m_bCBOffsettingSupported; }

private:
	void BuildAdapterDatabase();

	void CreatePostAAStaticResources();
	void UpdateAAConstants();
	void RefreshAvailableResolutions();

private:
	ID3D11Device*         m_pDevice         = TNULL; // NOTE: DUE TO COMPATIBILITY, IT NEEDS TO BE AT THIS OFFSET!!!
	ID3D11DeviceContext*  m_pDeviceContext  = TNULL; // NOTE: DUE TO COMPATIBILITY, IT NEEDS TO BE AT THIS OFFSET!!!

	// Things left from TRenderD3DInterface (D3D8)
	TBYTE                                PADDING1[ 84 ];
	TFLOAT                               m_fPixelAspectRatio; // Pixel aspect ratio
	HACCEL                               m_AcceleratorTable;  // Accelerator table
	Toshi::TRenderAdapter::Mode::Device* m_pAdapterDevice;    // Current device
	DISPLAYPARAMS                        m_oDisplayParams;    // Display parameters

	SDLWindow                           m_Window;                          // Window
	TBOOL                               m_bExited;                         // Exit flag
	TFLOAT                              m_fContrast;                       // Contrast value
	TFLOAT                              m_fBrightness;                     // Brightness value
	TFLOAT                              m_fGamma;                          // Gamma value
	TFLOAT                              m_fSaturate;                       // Saturation value
	TBOOL                               m_bChangedColourSettings;          // Color settings changed flag
	TBOOL                               m_bCheckedCapableColourCorrection; // Color correction capability checked flag
	TBOOL                               m_bCapableColourCorrection;        // Color correction capability flag
	TBOOL                               m_bEnableColourCorrection;         // Color correction enabled flag
	TBYTE                               PADDING2[ 1536 ];
	TBOOL                               m_bFailed;     // Failure flag
	void*                               m_Unk1;        // Unknown 1
	void*                               m_Unk2;        // Unknown 2
	Toshi::TPriList<Toshi::TOrderTable> m_OrderTables; // Order tables

	// D3D11 main objects
	D3D_FEATURE_LEVEL         m_eFeatureLevel;
	IDXGISwapChain*           m_pSwapChain               = TNULL;
	ID3D11Texture2D*          m_pSwapChainBackBuffer     = TNULL;
	ID3D11RenderTargetView*   m_pSwapChainBackBufferRTV  = TNULL;
	ID3D11Texture2D*          m_pPresentResolveTexture   = TNULL;
	ID3D11ShaderResourceView* m_pPresentResolveSRV       = TNULL;

	// LDR landing buffer the tone-map pass writes when post-AA is active (swapchain-sized)
	ID3D11Texture2D*          m_pAALDRTexture            = TNULL;
	ID3D11RenderTargetView*   m_pAALDRRTV                = TNULL;
	ID3D11ShaderResourceView* m_pAALDRSRV                = TNULL;
	// SMAA intermediate targets (swapchain-sized): edges and blend weights
	ID3D11Texture2D*          m_pSMAAEdgesTexture        = TNULL;
	ID3D11RenderTargetView*   m_pSMAAEdgesRTV            = TNULL;
	ID3D11ShaderResourceView* m_pSMAAEdgesSRV            = TNULL;
	ID3D11Texture2D*          m_pSMAABlendTexture        = TNULL;
	ID3D11RenderTargetView*   m_pSMAABlendRTV            = TNULL;
	ID3D11ShaderResourceView* m_pSMAABlendSRV            = TNULL;
	// SMAA precomputed lookup textures (created once; live for the device lifetime)
	ID3D11ShaderResourceView* m_pSMAAAreaSRV             = TNULL;
	ID3D11ShaderResourceView* m_pSMAASearchSRV           = TNULL;
	// AA constant buffer (PS slot b1): RT metrics = ( 1/w, 1/h, w, h )
	ID3D11Buffer*             m_pAAConstantBuffer        = TNULL;
	ID3D11RenderTargetView*   m_pRenderTargetView        = TNULL;
	ID3D11Texture2D*          m_pRenderTargetTexture     = TNULL;
	ID3D11ShaderResourceView* m_pRenderTargetSRV         = TNULL;
	ID3D11Texture2D*          m_pDepthStencilTexture     = TNULL;
	ID3D11DepthStencilView*   m_pDepthStencilView        = TNULL;
	ID3D11ShaderResourceView* m_pDepthStencilSRV         = TNULL;
	ID3D11RenderTargetView*   m_pGlowRenderTargetView    = TNULL;
	ID3D11Texture2D*          m_pGlowRenderTargetTexture = TNULL;
	ID3D11ShaderResourceView* m_pGlowRenderTargetSRV     = TNULL;
	// Main-pass G-buffer (MSAA): rgb = world-space normal, a = reflectivity.
	// Bound on slot 1 during the scene pass, then resolved and sampled by SSR.
	ID3D11Texture2D*          m_pGBufferTexture          = TNULL;
	ID3D11RenderTargetView*   m_pGBufferRTV              = TNULL;
	DXGI_SWAP_CHAIN_DESC      m_oSwapChainDesc;

	// TRUE when the swapchain was created with tearing support and Present
	// should pass DXGI_PRESENT_ALLOW_TEARING (see RENDER_ALLOW_TEARING).
	TBOOL m_bAllowTearing = TFALSE;

	// Actual MSAA sample count in use, clamped to what the device supports
	// (see GetSupportedMSAASampleCount). May be lower than MSAA_SAMPLE_COUNT.
	TUINT m_uiMSAASampleCount = 1;

	// Runtime graphics settings + pending-change tracking (see ApplyGraphicsSettings).
	GraphicsSettings m_oActiveSettings;
	GraphicsSettings m_oPendingSettings;
	TUINT            m_uiGraphicsDirty = GFX_DIRTY_NONE;
	TUINT            m_uiSyncInterval  = 0; // Present sync interval (0 = no vsync)

	Toshi::T2DynamicVector<Resolution> m_vecAvailableResolutions;

	ID3D11DeviceContext1* m_pDeviceContext1 = TNULL; // D3D11.1 context

	TracyD3D11Ctx m_pTracyD3D11Ctx = TNULL;

	// Font rendering
	// TODO: move this away from here
	FontAtlas* m_pFontAtlases[ 2 ];

	// Buffers
	void*         m_pVertexConstantBuffer;
	TBOOL         m_IsVertexConstantBufferUpdated;
	ID3D11Buffer* m_VertexBuffers[ NUMBUFFERS ];
	TSIZE         m_VertexBufferIndex;
	TSIZE         m_VertexBufferNewSize;
	TSIZE         m_VertexBufferCurSize;

	// Per-pass constant buffer (b4): CPU shadow copy + dirty flag (see PassBufferSetVec4)
	void*         m_pPassConstantBuffer  = TNULL;
	TBOOL         m_bPassBufferDirty     = TFALSE;
	ID3D11Buffer* m_pPassBuffer          = TNULL;

	// UpdatePassViewProj change-detection inputs (valid once the first pass ran)
	Toshi::TMatrix44 m_oPassProjection;
	Toshi::TMatrix44 m_oPassWorldView;
	TBOOL            m_bPassViewProjValid = TFALSE;

	TBOOL m_bCBOffsettingSupported = TFALSE; // D3D11.1 *SetConstantBuffers1 offsets usable

	ID3D11Buffer* m_MainVertexBuffer;
	TUINT         m_iImmediateVertexCurrentOffset;

	ID3D11Buffer* m_MainIndexBuffer;
	TUINT         m_iImmediateIndexCurrentOffset;

	ID3D11Buffer* m_pShadowConstantBuffer;

	ID3D11Buffer* m_pDepthPassConstantBuffer;

	ID3D11VertexShader* m_pScreenRectangleVertexShader;
	ID3D11PixelShader*  m_pRedTintPixelShader;
	ID3D11InputLayout*  m_pScreenRectangleInputLayout;

	// Various states
	TFLOAT              m_aClearColor[ 4 ];
	ID3D11SamplerState* m_aSamplerStates[ SAMPLER_COUNT ];
	CSMManager          m_oCSMManager;
	LightManager        m_oLightManager;

	// Depth states
	Toshi::T2Map<DepthState, ID3D11DepthStencilState*, DepthStateComparator> m_DepthStatesTree;
	DepthPair                                                                m_DepthState;
	DepthPair                                                                m_PreviousDepth;

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
	ID3D11Buffer*            m_pCurrentVertexBuffer[ 2 ];
	TUINT                    m_uiVBCurrentStride[ 2 ];
	TUINT                    m_uiVBCurrentOffset[ 2 ];

	ID3D11Buffer* m_pCurrentIndexBuffer;
	DXGI_FORMAT   m_eIBCurrentFormat;
	TUINT         m_uiIBCurrentOffset;

	ID3D11Buffer* m_aVSCurrentConstantBuffers[ 16 ];
	ID3D11Buffer* m_aPSCurrentConstantBuffers[ 16 ];
	// Cached *SetConstantBuffers1 ranges per slot (16-byte constants); 0/0 = whole-buffer bind
	TUINT         m_aVSCurrentCBFirst[ 16 ] = {};
	TUINT         m_aVSCurrentCBNum[ 16 ]   = {};
	TUINT         m_aPSCurrentCBFirst[ 16 ] = {};
	TUINT         m_aPSCurrentCBNum[ 16 ]   = {};

	ID3D11SamplerState* m_aVSCurrentSampleStates[ D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT ];
	ID3D11SamplerState* m_aPSCurrentSampleStates[ D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT ];

	ID3D11RenderTargetView* m_pCurrentRenderTargetView;
	ID3D11DepthStencilView* m_pCurrentDepthStencilView;
	ID3D11RenderTargetView* m_pSecondaryRenderTargetView = TNULL; // persistent slot-1 G-buffer RTV
	TBOOL                   m_bRenderTargetsDirty         = TFALSE;

	ID3D11VertexShader*         m_pCurrentVertexShader;
	ID3D11PixelShader*          m_pCurrentPixelShader;
	ID3D11InputLayout*          m_pCurrentInputLayout;
	mutable ID3D11VertexShader* m_pCurrentPipelineVertexShaderSlot;
	mutable ID3D11PixelShader*  m_pCurrentPipelinePixelShaderSlot;

	ID3D11ShaderResourceView* m_apShaderResourceViewsPS[ D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT ];
	ID3D11ShaderResourceView* m_apShaderResourceViewsVS[ D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT ];
};

extern RenderDX11* g_pRender;

} // namespace remaster
