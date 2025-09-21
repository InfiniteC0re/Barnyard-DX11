#pragma once
#include "Ref/ASysShader/ASysMaterial_DX8.h"

namespace remaster
{

class SysMaterial
	: public ASysMaterial
{
public:
	TDECLARE_CLASS( SysMaterial, ASysMaterial );

public:
	SysMaterial();
	~SysMaterial();

	//-----------------------------------------------------------------------------
	// Toshi::TMaterial
	//-----------------------------------------------------------------------------
	virtual void  PreRender() OVERRIDE;
	virtual void  PostRender() OVERRIDE;

	//-----------------------------------------------------------------------------
	// ASysMaterial
	//-----------------------------------------------------------------------------
	virtual TBOOL Create( BLENDMODE a_eBlendMode ) OVERRIDE;
	virtual void  SetBlendMode( BLENDMODE a_eBlendMode ) OVERRIDE;

	void SetOrderTable( Toshi::TOrderTable* a_pOrderTable );

private:
	Toshi::TOrderTable* m_pAssignedOrderTable;
};


}