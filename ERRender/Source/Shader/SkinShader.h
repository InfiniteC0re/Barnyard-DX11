#pragma once
#include "Ref/ASkinShader/ASkinShader_DX8.h"

namespace remaster
{

void SetupRenderHooks_SkinShader();

class SkinShaderDX11 : public ASkinShader
{
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
};

}; // namespace remaster
