#pragma once
#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "WorldMaterial.h"
#include "Ref/AWorldShader/AWorldShader_DX8.h"

#include <d3d11.h>
#include <ToshiTools/T2DynamicVector.h>

namespace remaster
{

void SetupRenderHooks_WorldShader();

class WorldShaderDX11
    : public AWorldShader
{
public:
	TDECLARE_CLASS( WorldShaderDX11, AWorldShader );

	static constexpr TUINT NUM_ORDER_TABLES = 9;

	struct AUnknown : Toshi::TNodeList<AUnknown>::TNode
	{
	};

public:
	WorldShaderDX11();
	~WorldShaderDX11();

	//-----------------------------------------------------------------------------
	// Toshi::TShader
	//-----------------------------------------------------------------------------
	virtual void  Flush() override;
	virtual void  StartFlush() override;
	virtual void  EndFlush() override;
	virtual TBOOL Create() override;
	virtual TBOOL Validate() override;
	virtual void  Invalidate() override;
	virtual TBOOL TryInvalidate() override;
	virtual TBOOL TryValidate() override;
	virtual void  Render( Toshi::TRenderPacket* a_pRenderPacket ) override;

	//-----------------------------------------------------------------------------
	// AWorldShader
	//-----------------------------------------------------------------------------
	virtual void EnableRenderEnvMap( TBOOL a_bEnable ) override;

	//-----------------------------------------------------------------------------
	// Own methods
	//-----------------------------------------------------------------------------

	// Returns TTRUE if alpha blend material is enabled
	virtual TBOOL IsAlphaBlendMaterial() override;

	// Enabled alpha blend material
	virtual void SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial ) override;

	// Creates AWorldMaterial and returns it
	virtual AWorldMaterial* CreateMaterial( const TCHAR* a_szName ) override;

	// Creates AWorldMesh and returns it
	virtual AWorldMesh* CreateMesh( const TCHAR* a_szName ) override;

	// Returns TTRUE if shaders are supported
	virtual TBOOL IsHighEndMode();

	// Enables high end mode (compiled shader) if they are supported by hardware
	virtual void SetHighEndMode( TBOOL a_bEnable );

	// Returns TTRUE if shaders are supported by hardware
	virtual TBOOL IsCapableShaders();

	// Returns TTRUE if rendering of env map is enabled
	virtual TBOOL IsRenderEnvMapEnabled();

	// Probably used in debug mode but is stripped out in release
	virtual void* CreateUnknown( void*, void*, void*, void* );

private:
	void UploadDynamicGlowLights( Toshi::TRenderPacket* a_pRenderPacket );

public:
	Toshi::TOrderTable* GetOrderTable( TUINT a_uiIndex )
	{
		TASSERT( a_uiIndex < NUM_ORDER_TABLES );
		return &m_aOrderTables[ a_uiIndex ];
	}

	Toshi::TOrderTable* GetShadowOrderTable()
	{
		return &m_oShadowTable;
	}

	remaster::WorldMaterial* GetShadowMaterial()
	{
		return &m_oDummyMaterial;
	}

	const Toshi::TVector4& GetShadowColour() const { return m_ShadowColour; }
	const Toshi::TVector4& GetAmbientColour() const { return m_AmbientColour; }

	void SetColours( const Toshi::TVector4& a_rShadowColour, const Toshi::TVector4 a_rAmbientColour )
	{
		m_ShadowColour  = a_rShadowColour;
		m_AmbientColour = a_rAmbientColour;

		Toshi::TMath::Clip( m_ShadowColour.x, 0.0f, 1.0f );
		Toshi::TMath::Clip( m_ShadowColour.y, 0.0f, 1.0f );
		Toshi::TMath::Clip( m_ShadowColour.z, 0.0f, 1.0f );
		Toshi::TMath::Clip( m_AmbientColour.x, 0.0f, 1.0f );
		Toshi::TMath::Clip( m_AmbientColour.y, 0.0f, 1.0f );
		Toshi::TMath::Clip( m_AmbientColour.z, 0.0f, 1.0f );
	}

private:
	Toshi::TNodeList<AUnknown> m_SomeList;
	DWORD                      m_hVertexShader;
	TINT*                      m_pUnk2;
	TUINT                      m_iAlphaRef;
	TBOOL                      m_bRenderEnvMap;
	Toshi::TOrderTable         m_aOrderTables[ NUM_ORDER_TABLES ];
	TBOOL                      m_bIsHighEndMode;
	TBOOL                      m_bAlphaBlendMaterial;
	TBOOL                      m_bUnkFlag3;
	TBOOL                      m_bUnkFlag4;
	Toshi::TVector4            m_ShadowColour;
	Toshi::TVector4            m_AmbientColour;

	Toshi::T2DynamicVector<RenderDX11::ShaderPipelineState> m_vecWorldPipelines;
	Toshi::T2DynamicVector<RenderDX11::ShaderPipelineState> m_vecShadowDepthPipelines;

	inline static Toshi::TOrderTable      m_oShadowTable;
	inline static remaster::WorldMaterial m_oDummyMaterial;
};

}; // namespace remaster
