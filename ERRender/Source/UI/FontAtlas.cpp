#include "pch.h"
#include "FontAtlas.h"
#include "FontCache.h"
#include "RenderDX11.h"
#include "RenderDX11Text.h"
#include "RenderDX11Utils.h"
#include "UIRenderer.h"

#include <Toshi/TColor.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static FT_Library s_FontLibrary;

remaster::FontAtlas::FontAtlas( ID3D11ShaderResourceView* a_pAtlasSRV, ID3D11Texture2D* a_pAtlas, TUINT a_uiWidth, TUINT a_uiHeight )
    : m_pAtlasSRV( a_pAtlasSRV )
    , m_pAtlas( a_pAtlas )
    , m_uiWidth( a_uiWidth )
    , m_uiHeight( a_uiHeight )
    , m_flBaseScale( 32.0f )
    , m_flHeightOffset( 8.0f )
{
	DX11_API_VALIDATE( g_pRender->GetD3D11Device()->CreateRenderTargetView( m_pAtlas, TNULL, &m_pAtlasTargetView ) );

	if ( !s_FontLibrary )
	{
		FT_Init_FreeType( &s_FontLibrary );
	}

	// Load font
	FT_New_Face( s_FontLibrary, "CCThatsAllFolks.ttf", 0, &m_oFontFace );
	FT_Set_Pixel_Sizes( m_oFontFace, 0, 64 );

	// Add refs to the gx resources
	m_pAtlasSRV->AddRef();
	m_pAtlas->AddRef();

	// Initialise partition tree
	m_oPartitionTree.width       = TFLOAT( a_uiWidth );
	m_oPartitionTree.height      = TFLOAT( a_uiHeight );
	m_oPartitionTree.root.width  = m_oPartitionTree.width;
	m_oPartitionTree.root.height = m_oPartitionTree.height;
	m_oPartitionTree.root.x      = 0;
	m_oPartitionTree.root.y      = 0;

	// Reset all characters in the map array
	for ( auto& val : m_aCharMap ) val = -1;
}

remaster::FontAtlas::~FontAtlas()
{
	if ( m_pStrokeStyle ) m_pStrokeStyle->Release();
	if ( m_pAtlasSRV ) m_pAtlasSRV->Release();
	if ( m_pAtlas ) m_pAtlas->Release();
}

void remaster::FontAtlas::GetCharUV( TWCHAR a_wChar, TFLOAT a_flScale, CharInfo& a_rCharInfo )
{
	TUINT16& rCharIndex = m_aCharMap[ a_wChar ];

	TFLOAT flFontSize = a_flScale * m_flBaseScale;

	if ( rCharIndex == 65535 )
	{
		// This character is seen for the first time
		CachedChar* pCachedChar = new CachedChar();

		m_vecCachedChars.PushBack( pCachedChar );
		rCharIndex = m_vecCachedChars.Size() - 1;
	}
	else
	{
		// Check if there is this exact scale of the character in the cache

		CachedChar* pCachedChar = m_vecCachedChars[ rCharIndex ];
		T2_FOREACH( pCachedChar->m_vecChars, it )
		{
			if ( *it == flFontSize )
			{
				// Found the character, early out the function
				a_rCharInfo.flWidth   = it->pTreeNode->width;
				a_rCharInfo.flHeight  = it->pTreeNode->height;
				a_rCharInfo.flUV1X    = it->flUV1X;
				a_rCharInfo.flUV1Y    = it->flUV1Y;
				a_rCharInfo.flUV2X    = it->flUV2X;
				a_rCharInfo.flUV2Y    = it->flUV2Y;
				a_rCharInfo.iAdvanceX = it->iAdvanceX;
				a_rCharInfo.iAdvanceY = it->iAdvanceY;
				a_rCharInfo.iBearingX = it->iBearingX;
				a_rCharInfo.iBearingY = it->iBearingY;

				return;
			}
		}
	}

	///////////////////////////////////////////////////////////////////////////////
	//// Create cache for the character
	///////////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------------
	// 1. Draw to the atlas texture
	//-----------------------------------------------------------------------------

	FT_UInt  glyph_index = FT_Get_Char_Index( m_oFontFace, a_wChar );
	FT_Error error       = FT_Load_Glyph( m_oFontFace, glyph_index, FT_LOAD_DEFAULT );
	FT_Render_Glyph( m_oFontFace->glyph, FT_RENDER_MODE_NORMAL );

	TUINT uiCharWidth  = m_oFontFace->glyph->bitmap.width;
	TUINT uiCharHeight = m_oFontFace->glyph->bitmap.rows;
	TBOOL bHasTexture  = uiCharWidth + uiCharHeight > 0;
	
	if ( !bHasTexture )
	{
		uiCharWidth  = 1;
		uiCharHeight = 1;
	}

	PartitionTreeNode* pTreeNode = PartitionTree_AddNode( &m_oPartitionTree, TFLOAT( uiCharWidth ), TFLOAT( uiCharHeight ) );

	if ( bHasTexture )
	{
		ID3D11ShaderResourceView* pCharSRV = remaster::dx11::CreateTexture( uiCharWidth, uiCharHeight, DXGI_FORMAT_R8_UINT, m_oFontFace->glyph->bitmap.buffer, D3D11_USAGE_IMMUTABLE, 0, 1 );

		ID3D11Resource* pCharRes;
		pCharSRV->GetResource( &pCharRes );

		D3D11_BOX oBox;
		oBox.left   = 0;
		oBox.right  = m_oFontFace->glyph->bitmap.width;
		oBox.top    = 0;
		oBox.bottom = m_oFontFace->glyph->bitmap.rows;
		oBox.back   = 1;
		oBox.front  = 0;

		g_pRender->GetD3D11DeviceContext()->CopySubresourceRegion(
			m_pAtlas,
			0,
		    TUINT( pTreeNode->x ),
		    TUINT( pTreeNode->y ),
			0,
		    pCharRes,
			0,
		    &oBox
		);
	}

	//-----------------------------------------------------------------------------
	// 2. Fill information of this cached character
	//-----------------------------------------------------------------------------

	ScaledChar oScaledChar;
	oScaledChar.pTreeNode = pTreeNode;
	oScaledChar.flScale   = flFontSize;
	oScaledChar.flUV1X    = ( pTreeNode->x ) / m_uiWidth;
	oScaledChar.flUV1Y    = ( pTreeNode->y ) / m_uiHeight;
	oScaledChar.flUV2X    = ( pTreeNode->x + uiCharWidth ) / m_uiWidth;
	oScaledChar.flUV2Y    = ( pTreeNode->y + uiCharHeight ) / m_uiHeight;
	oScaledChar.iAdvanceX = m_oFontFace->glyph->advance.x;
	oScaledChar.iAdvanceY = m_oFontFace->glyph->advance.y;
	oScaledChar.iBearingX = m_oFontFace->glyph->bitmap_left;
	oScaledChar.iBearingY = m_oFontFace->glyph->bitmap_top;
	m_vecCachedChars[ rCharIndex ]->m_vecChars.Push( oScaledChar );

	a_rCharInfo.flWidth   = oScaledChar.pTreeNode->width;
	a_rCharInfo.flHeight  = oScaledChar.pTreeNode->height;
	a_rCharInfo.flUV1X    = oScaledChar.flUV1X;
	a_rCharInfo.flUV1Y    = oScaledChar.flUV1Y;
	a_rCharInfo.flUV2X    = oScaledChar.flUV2X;
	a_rCharInfo.flUV2Y    = oScaledChar.flUV2Y;
	a_rCharInfo.iAdvanceX = oScaledChar.iAdvanceX;
	a_rCharInfo.iAdvanceY = oScaledChar.iAdvanceY;
	a_rCharInfo.iBearingX = oScaledChar.iBearingX;
	a_rCharInfo.iBearingY = oScaledChar.iBearingY;
}

TFLOAT remaster::FontAtlas::GetTextWidth( const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flScale )
{
	TFLOAT flResult       = 0.0f;
	TFLOAT flScaleXFactor = 1.0f / remaster::g_pUIRender->GetScaleX();

	for ( TSIZE i = 0; i < a_uiTextLength; i++ )
	{
		remaster::FontAtlas::CharInfo oCharInfo;
		GetCharUV( a_wcsText[ i ], a_flScale, oCharInfo );

		flResult += ( oCharInfo.iAdvanceX >> 6 ) * flScaleXFactor;
	}

	return flResult;
}

TFLOAT remaster::FontAtlas::GetTextHeight( const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flScale )
{
	TFLOAT flResult       = 0.0f;
	TFLOAT flScaleYFactor = 1.0f / remaster::g_pUIRender->GetScaleY();

	for ( TSIZE i = 0; i < a_uiTextLength; i++ )
	{
		remaster::FontAtlas::CharInfo oCharInfo;
		GetCharUV( a_wcsText[ i ], a_flScale, oCharInfo );

		flResult = TMath::Max( flResult, oCharInfo.flHeight * flScaleYFactor );
	}

	return flResult;
}
