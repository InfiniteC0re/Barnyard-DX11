#pragma once
#include "Ref/AWorldShader/AWorldMaterial_DX8.h"

namespace remaster
{

class WorldMaterial
    : public AWorldMaterial
{
public:
	TDECLARE_CLASS( WorldMaterial, AWorldMaterial );

public:
	WorldMaterial();
	~WorldMaterial();

	//-----------------------------------------------------------------------------
	// Toshi::TMaterial
	//-----------------------------------------------------------------------------
	virtual void OnDestroy() override;
	virtual void PreRender() override;
	virtual void PostRender() override;

	//-----------------------------------------------------------------------------
	// AWorldMaterial
	//-----------------------------------------------------------------------------
	virtual TBOOL Create( BLENDMODE a_eBlendMode ) override;
	virtual void  SetBlendMode( BLENDMODE a_eBlendMode ) override;

	//-----------------------------------------------------------------------------
	// Own methods
	//-----------------------------------------------------------------------------
	virtual void CopyToAlphaBlendMaterial();

	void SetOrderTable( Toshi::TOrderTable* a_pOrderTable, TINT a_iUnused = 0 );

private:
	WorldMaterial*      m_pAlphaBlendMaterial;
	Toshi::TOrderTable* m_pAssignedOrderTable;
	TBOOL               m_aHasUVOffsets[ MAX_TEXTURES ];
	TFLOAT              m_aUVOffsetsX[ MAX_TEXTURES ];
	TFLOAT              m_aUVOffsetsY[ MAX_TEXTURES ];
};

}; // namespace remaster
