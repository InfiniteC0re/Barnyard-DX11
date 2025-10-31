#pragma once
#include "Hash.h"
#include "PartitionTree.h"

#include <Toshi/TString16.h>
#include <Toshi/T2DList.h>
#include <Toshi/T2SortedVector.h>
#include <ToshiTools/T2DynamicVector.h>

#include <freetype/freetype.h>

#include <d2d1.h>
#include <d3d11.h>

namespace remaster
{

class FontAtlas
{
public:
	inline static constexpr TFLOAT FONT_SCALE_SIMILARITY_THRESHOLD = 0.5f;

	struct ScaledChar
	{
		PartitionTreeNode* pTreeNode;
		TFLOAT             flScale;
		TFLOAT             flUV1X;
		TFLOAT             flUV1Y;
		TFLOAT             flUV2X;
		TFLOAT             flUV2Y;

		TBOOL operator==( TFLOAT a_flScale ) { return flScale == a_flScale; }
	};

	struct CharInfo
	{
		TFLOAT flWidth;
		TFLOAT flHeight;
		TFLOAT flUV1X;
		TFLOAT flUV1Y;
		TFLOAT flUV2X;
		TFLOAT flUV2Y;
	};

	struct ScaledCharSortResults
	{
		TINT operator()( const ScaledChar& a_rcVal1, const ScaledChar& a_rcVal2 ) const
		{
			if ( a_rcVal1.flScale < a_rcVal2.flScale )
				return 1;
			else if ( fabs( a_rcVal1.flScale - a_rcVal2.flScale ) <= FONT_SCALE_SIMILARITY_THRESHOLD )
				return 0;

			return -1;
		}
	};

	struct CachedChar
	{
		Toshi::T2SortedVector<ScaledChar, Toshi::T2DynamicVector<ScaledChar>, ScaledCharSortResults> m_vecChars;
	};

public:
	FontAtlas( ID3D11ShaderResourceView* a_pAtlasSRV, ID3D11Texture2D* a_pAtlas, TUINT a_uiWidth, TUINT a_uiHeight );
	~FontAtlas();

	void GetCharUV( TWCHAR a_wChar, TFLOAT a_flScale, CharInfo& a_rCharInfo );

	TFLOAT GetTextWidth( const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flScale );
	TFLOAT GetTextHeight( const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flScale );
	TFLOAT GetOutlineSize( TFLOAT a_flScale ) const { return m_flBaseScale * a_flScale * ( 1.0f / 42.0f ) * 6.0f; }
	//TFLOAT GetOutlineSize( TFLOAT a_flScale ) const { return 0.0f; }

	TFLOAT                    GetBaseScale() const { return m_flBaseScale; }
	TFLOAT                    GetHeightOffset() const { return m_flHeightOffset; }
	TFLOAT                    GetSpriteMargin() const { return m_flSpriteMargin; }
	ID3D11ShaderResourceView* GetTextureResource() const { return m_pAtlasSRV; }

private:
	FT_Face m_oFontFace;

	ID3D11ShaderResourceView* m_pAtlasSRV;
	ID3D11Texture2D*          m_pAtlas;
	ID2D1StrokeStyle*         m_pStrokeStyle;
	TUINT                     m_uiWidth;
	TUINT                     m_uiHeight;

	TFLOAT m_flSpriteMargin;
	TFLOAT m_flBaseScale;
	TFLOAT m_flHeightOffset;

	PartitionTree m_oPartitionTree;

	Toshi::T2DynamicVector<CachedChar*> m_vecCachedChars;
	TUINT16                             m_aCharMap[ 65535 + 1 ];
};

} // namespace remaster
