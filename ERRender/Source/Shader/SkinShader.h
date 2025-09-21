#pragma once
#include "Ref/ASkinShader/ASkinShader_DX8.h"

namespace remaster
{

void SetupRenderHooks_SkinShader();

class SkinShaderDX11 : public ASkinShader
{
public:
	TDECLARE_CLASS( SkinShaderDX11, ASkinShader );

	struct AUnknown : Toshi::TNodeList<AUnknown>::TNode
	{
	};

	static constexpr TUINT NUM_ORDER_TABLES = 3;

public:
	SkinShaderDX11();
	~SkinShaderDX11();

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
	// ASkinShader
	//-----------------------------------------------------------------------------
	virtual void           EnableRenderEnvMap( TBOOL a_bEnable ) override;
	virtual TBOOL          IsHighEndSkinning() override;
	virtual void           EnableHighEndSkinning( TBOOL a_bEnable ) override;
	virtual TBOOL          IsCapableHighEndSkinning() override;
	virtual TBOOL          IsLightScattering() override;
	virtual void           SetLightScattering( TBOOL a_bEnable ) override;
	virtual TBOOL          IsAlphaBlendMaterial() override;
	virtual void           SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial ) override;
	virtual ASkinMaterial* CreateMaterial( const TCHAR* a_szName ) override;
	virtual ASkinMesh*     CreateMesh( const TCHAR* a_szName ) override;
	virtual TINT           AddLight( const Toshi::TVector3& a_rPosition, TFLOAT a_fIntensity ) override;
	virtual void           SetLight( TINT a_iIndex, const Toshi::TVector3& a_rPosition, TFLOAT a_fIntensity ) override;
	virtual void           RemoveLight( TINT a_iIndex ) override;

	//-----------------------------------------------------------------------------
	// Own methods
	//-----------------------------------------------------------------------------
	virtual TBOOL IsEnableRenderEnvMap();
	virtual void  SetSomeColour( TUINT a_uiR, TUINT a_uiG, TUINT a_uiB, TUINT a_uiA );
	virtual TINT  SetUnknown1( TINT a_Unknown, TUINT8 a_fAlpha );
	virtual void  SetUnknown2( TINT a_Unknown );

public:
	Toshi::TOrderTable* GetOrderTable( TUINT a_uiIndex )
	{
		TASSERT( a_uiIndex < NUM_ORDER_TABLES );
		return &m_aOrderTables[ a_uiIndex ];
	}

private:
	TCHAR                      PADDING[ 44 ];
	Toshi::TNodeList<AUnknown> m_SomeList;
	DWORD                      m_hUnknownPixelShader;
	DWORD                      m_hVertexShader;
	DWORD                      m_hVertexShaderHD;
	DWORD                      m_hPixelShader;
	TINT                       m_iAlphaRef;
	TBOOL                      m_bRenderEnvMap;
	Toshi::TOrderTable         m_aOrderTables[ NUM_ORDER_TABLES ];
	TBOOL                      m_bHighEndSkinning;
	TBOOL                      m_bLightScattering;
	TBOOL                      m_bIsAlphaBlendMaterial;
	TFLOAT                     m_fAMDPatch1;
	TFLOAT                     m_fAMDPatch2;
	TBOOL                      m_bCPUSupportsFeature1;
	TBOOL                      m_bCPUSupportsFeature2;
	TBOOL                      m_bUnkFlag;
};

}; // namespace remaster
