#pragma once
#include "Ref/ASkinShader/ASkinMaterial_DX8.h"

namespace remaster
{

class SkinMaterial
	: public ASkinMaterial
{
public:
	TDECLARE_CLASS( SkinMaterial, ASkinMaterial );

public:
	SkinMaterial();
	~SkinMaterial();

	//-----------------------------------------------------------------------------
	// Toshi::TMaterial
	//-----------------------------------------------------------------------------
	virtual void OnDestroy() override;
	virtual void PreRender() override;
	virtual void PostRender() override;

	//-----------------------------------------------------------------------------
	// ASkinMaterial
	//-----------------------------------------------------------------------------
	virtual TBOOL Create( BLENDMODE a_eBlendMode ) override;
	virtual void  SetBlendMode( BLENDMODE a_eBlendMode ) override;

	//-----------------------------------------------------------------------------
	// Own methods
	//-----------------------------------------------------------------------------
	virtual void CopyToAlphaBlendMaterial();

public:
	void SetOrderTable( Toshi::TOrderTable* a_pOrderTable );
	
private:
	SkinMaterial*       m_pAlphaBlendMaterial;
	Toshi::TOrderTable* m_pAssignedOrderTable;
	TBOOL               m_bIsHDLighting;
	TBOOL               m_bHasLighting1Tex;
	TBOOL               m_bHasLighting2Tex;
	Toshi::TTexture*    m_pSomeTexture;
};

}; // namespace remaster
