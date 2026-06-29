#pragma once
#include "Ref/AWorldShader/AWorldMaterial_DX8.h"
#include "MaterialParams.h"

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
	virtual TBOOL CreateDummy();
	virtual void  CopyToAlphaBlendMaterial();

	void SetOrderTable( Toshi::TOrderTable* a_pOrderTable, TINT a_iUnused = 0 );

	void             SetMaterialParams( MaterialParams* a_pParams ) { m_pMatParams = a_pParams; }
	MaterialParams*& GetMaterialParams() { return m_pMatParams; }

	TFLOAT GetUVOffsetX( TUINT a_uiTextureIndex )
	{
		TASSERT( a_uiTextureIndex < MAX_TEXTURES );
		return m_aUVOffsetsX[ a_uiTextureIndex ];
	}

	void SetUVOffsetX( TUINT a_uiTextureIndex, TFLOAT a_fOffset )
	{
		TASSERT( a_uiTextureIndex < MAX_TEXTURES );
		m_aUVOffsetsX[ a_uiTextureIndex ] = a_fOffset;
	}

	void AddUVOffsetX( TUINT a_uiTextureIndex, TFLOAT a_fOffset )
	{
		TASSERT( a_uiTextureIndex < MAX_TEXTURES );
		m_aUVOffsetsX[ a_uiTextureIndex ] += a_fOffset;
	}

	TFLOAT GetUVOffsetY( TUINT a_uiTextureIndex )
	{
		TASSERT( a_uiTextureIndex < MAX_TEXTURES );
		return m_aUVOffsetsY[ a_uiTextureIndex ];
	}

	void SetUVOffsetY( TUINT a_uiTextureIndex, TFLOAT a_fOffset )
	{
		TASSERT( a_uiTextureIndex < MAX_TEXTURES );
		m_aUVOffsetsY[ a_uiTextureIndex ] = a_fOffset;
	}

	void AddUVOffsetY( TUINT a_uiTextureIndex, TFLOAT a_fOffset )
	{
		TASSERT( a_uiTextureIndex < MAX_TEXTURES );
		m_aUVOffsetsY[ a_uiTextureIndex ] += a_fOffset;
	}

private:
	WorldMaterial*      m_pAlphaBlendMaterial;
	Toshi::TOrderTable* m_pAssignedOrderTable;
	TBOOL               m_aHasUVOffsets[ MAX_TEXTURES ];
	TFLOAT              m_aUVOffsetsX[ MAX_TEXTURES ];
	TFLOAT              m_aUVOffsetsY[ MAX_TEXTURES ];

	MaterialParams* m_pMatParams = TNULL;
};

}; // namespace remaster
