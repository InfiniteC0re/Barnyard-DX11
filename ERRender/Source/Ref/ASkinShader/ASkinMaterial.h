#pragma once
#include <Render/TMaterial.h>
#include <Platform/DX8/TTextureResourceHAL_DX8.h>

class ASkinMaterial : public Toshi::TMaterial
{
	TDECLARE_CLASS( ASkinMaterial, Toshi::TMaterial );

public:
	using ELightingTexture = TUINT;
	enum ELightingTexture_ : ELightingTexture
	{
		LT_0,
		LT_1,
		LT_2,
		LT_3,
		LT_NUMOF,
	};

	using BLENDMODE = TUINT;

	friend class AModelLoader;

public:
	ASkinMaterial();
	~ASkinMaterial();

	//-----------------------------------------------------------------------------
	// Own methods
	//-----------------------------------------------------------------------------
	virtual TBOOL Create( BLENDMODE a_eBlendMode );
	virtual void  SetBlendMode( BLENDMODE a_eBlendMode );

	BLENDMODE GetBlendMode() const { return m_eBlendMode; }

	void SetTexture( Toshi::TTexture* a_pTexture )
	{
		m_pTexture = a_pTexture;
		SetTextureNum( 1 );
	}

	Toshi::TTextureResourceHAL* GetLightingTexture( ELightingTexture a_eTexture )
	{
		TASSERT( a_eTexture < LT_NUMOF );
		return TSTATICCAST( Toshi::TTextureResourceHAL, m_apLightingTextures[ a_eTexture ] );
	}

	void SetLightingTexture( ELightingTexture a_eTex, Toshi::TTexture* a_pTexture )
	{
		m_apLightingTextures[ a_eTex ] = a_pTexture;
	}

protected:
	Toshi::TTexture* m_pTexture;
	Toshi::TTexture* m_apLightingTextures[ LT_NUMOF ];
	BLENDMODE        m_eBlendMode;
	TBOOL            m_bFlag;
};
