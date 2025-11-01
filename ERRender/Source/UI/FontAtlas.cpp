#include "pch.h"
#include "FontAtlas.h"
#include "RenderDX11.h"
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
    , m_flBaseScale( 1.5f )
    , m_flHeightOffset( 8.0f )
    , m_flLineHeightOffset( 8.0f )
    , m_flSDFMarginSize( 16.0f )
    , m_iLineSpacing( 2 * 64 )
{
	DX11_API_VALIDATE( g_pRender->GetD3D11Device()->CreateRenderTargetView( m_pAtlas, TNULL, &m_pAtlasTargetView ) );

	if ( !s_FontLibrary )
	{
		FT_Init_FreeType( &s_FontLibrary );
	}

	// Load font
	FT_New_Face( s_FontLibrary, "CCThatsAllFolks.ttf", 0, &m_oFontFace );
	FT_Set_Pixel_Sizes( m_oFontFace, 0, 32 );

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
		CachedChar* pCachedChar = m_vecCachedChars[ rCharIndex ];
		a_rCharInfo.flWidth     = pCachedChar->pTreeNode->width * flFontSize;
		a_rCharInfo.flHeight    = pCachedChar->pTreeNode->height * flFontSize;
		a_rCharInfo.flUV1X      = pCachedChar->flUV1X;
		a_rCharInfo.flUV1Y      = pCachedChar->flUV1Y;
		a_rCharInfo.flUV2X      = pCachedChar->flUV2X;
		a_rCharInfo.flUV2Y      = pCachedChar->flUV2Y;
		a_rCharInfo.iAdvanceX   = TINT( ( pCachedChar->iAdvanceX + m_iLineSpacing ) * flFontSize );
		a_rCharInfo.iAdvanceY   = TINT( pCachedChar->iAdvanceY * flFontSize );
		a_rCharInfo.iBearingX   = TINT( pCachedChar->iBearingX * flFontSize );
		a_rCharInfo.iBearingY   = TINT( pCachedChar->iBearingY * flFontSize );
		return;
	}

	///////////////////////////////////////////////////////////////////////////////
	//// Create cache for the character
	///////////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------------
	// 1. Draw to the atlas texture
	//-----------------------------------------------------------------------------

	FT_UInt  glyph_index = FT_Get_Char_Index( m_oFontFace, a_wChar );
	FT_Error error       = FT_Load_Glyph( m_oFontFace, glyph_index, FT_LOAD_DEFAULT );
	FT_Render_Glyph( m_oFontFace->glyph, FT_RENDER_MODE_SDF );

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

		pCharSRV->Release();
		pCharRes->Release();
	}

	//-----------------------------------------------------------------------------
	// 2. Fill information of this cached character
	//-----------------------------------------------------------------------------

	CachedChar* pCachedChar = m_vecCachedChars[ rCharIndex ];
	pCachedChar->pTreeNode  = pTreeNode;
	pCachedChar->flScale    = flFontSize;
	pCachedChar->flUV1X     = ( pTreeNode->x ) / m_uiWidth;
	pCachedChar->flUV1Y     = ( pTreeNode->y ) / m_uiHeight;
	pCachedChar->flUV2X     = ( pTreeNode->x + uiCharWidth ) / m_uiWidth;
	pCachedChar->flUV2Y     = ( pTreeNode->y + uiCharHeight ) / m_uiHeight;
	pCachedChar->iAdvanceX  = m_oFontFace->glyph->advance.x;
	pCachedChar->iAdvanceY  = m_oFontFace->glyph->advance.y;
	pCachedChar->iBearingX  = m_oFontFace->glyph->bitmap_left;
	pCachedChar->iBearingY  = m_oFontFace->glyph->bitmap_top;

	a_rCharInfo.flWidth   = pCachedChar->pTreeNode->width * flFontSize;
	a_rCharInfo.flHeight  = pCachedChar->pTreeNode->height * flFontSize;
	a_rCharInfo.flUV1X    = pCachedChar->flUV1X;
	a_rCharInfo.flUV1Y    = pCachedChar->flUV1Y;
	a_rCharInfo.flUV2X    = pCachedChar->flUV2X;
	a_rCharInfo.flUV2Y    = pCachedChar->flUV2Y;
	a_rCharInfo.iAdvanceX = TINT( ( pCachedChar->iAdvanceX + m_iLineSpacing ) * flFontSize );
	a_rCharInfo.iAdvanceY = TINT( pCachedChar->iAdvanceY * flFontSize );
	a_rCharInfo.iBearingX = TINT( pCachedChar->iBearingX * flFontSize );
	a_rCharInfo.iBearingY = TINT( pCachedChar->iBearingY * flFontSize );
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
