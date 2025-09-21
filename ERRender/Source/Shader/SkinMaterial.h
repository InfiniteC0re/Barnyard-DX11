#pragma once
#include "Ref/ASkinShader/ASkinMaterial_DX8.h"

namespace remaster
{

class SkinMaterial
	: public ASkinMaterial
{
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
};

}; // namespace remaster
