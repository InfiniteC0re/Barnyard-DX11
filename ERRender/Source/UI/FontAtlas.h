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

	struct CachedChar
	{
		PartitionTreeNode* pTreeNode;
		TFLOAT             flScale;
		TFLOAT             flUV1X;
		TFLOAT             flUV1Y;
		TFLOAT             flUV2X;
		TFLOAT             flUV2Y;
		TINT               iAdvanceX;
		TINT               iAdvanceY;
		TINT               iBearingX;
		TINT               iBearingY;

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
		TINT   iAdvanceX;
		TINT   iAdvanceY;
		TINT   iBearingX;
		TINT   iBearingY;
	};

	struct ScaledCharSortResults
	{
		TINT operator()( const CachedChar& a_rcVal1, const CachedChar& a_rcVal2 ) const
		{
			if ( a_rcVal1.flScale < a_rcVal2.flScale )
				return 1;
			if ( fabs( a_rcVal1.flScale - a_rcVal2.flScale ) <= FONT_SCALE_SIMILARITY_THRESHOLD )
				return 0;

			return -1;
		}
	};

public:
	FontAtlas( ID3D11ShaderResourceView* a_pAtlasSRV, const TCHAR* a_pchFileName, ID3D11Texture2D* a_pAtlas, TUINT a_uiWidth, TUINT a_uiHeight, TFLOAT a_flBaseScale = 1.8f, TFLOAT a_flHeightOffset = 14.0f, TFLOAT a_flLineHeightOffset = 9.0f );
	~FontAtlas();

	void GetCharUV( TWCHAR a_wChar, TFLOAT a_flScale, CharInfo& a_rCharInfo );

	TFLOAT GetTextWidth( const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flScale );
	TFLOAT GetTextHeight( const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flScale );

	TFLOAT                    GetBaseScale() const { return m_flBaseScale; }
	TFLOAT                    GetHeightOffset() const { return m_flHeightOffset; }
	TFLOAT                    GetLineHeightOffset() const { return m_flLineHeightOffset; }
	TFLOAT                    GetSDFMarginSize() const { return m_flSDFMarginSize; }
	ID3D11ShaderResourceView* GetTextureResource() const { return m_pAtlasSRV; }

private:
	FT_Face m_oFontFace;

	ID3D11RenderTargetView*   m_pAtlasTargetView;
	ID3D11ShaderResourceView* m_pAtlasSRV;
	ID3D11Texture2D*          m_pAtlas;
	ID2D1StrokeStyle*         m_pStrokeStyle;
	TUINT                     m_uiWidth;
	TUINT                     m_uiHeight;

	TFLOAT m_flBaseScale;
	TFLOAT m_flHeightOffset;
	TFLOAT m_flLineHeightOffset;
	TFLOAT m_flSDFMarginSize;

	TINT m_iLetterSpacing;

	PartitionTree m_oPartitionTree;

	Toshi::T2DynamicVector<CachedChar*> m_vecCachedChars;
	TUINT16                             m_aCharMap[ 65535 + 1 ];
};

} // namespace remaster
