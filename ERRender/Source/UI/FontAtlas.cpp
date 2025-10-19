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

remaster::FontAtlas::FontAtlas( ID3D11ShaderResourceView* a_pAtlasSRV, ID3D11Texture2D* a_pAtlas, TUINT a_uiWidth, TUINT a_uiHeight )
    : m_pAtlasSRV( a_pAtlasSRV )
    , m_pAtlas( a_pAtlas )
    , m_uiWidth( a_uiWidth )
    , m_uiHeight( a_uiHeight )
    , m_flBaseScale( 32.0f )
    , m_flHeightOffset( 8.0f )
    , m_flSpriteMargin( 0.0f )
{
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

	// Create stroke style for the text outline
	g_pRender->GetD2DFactory()->CreateStrokeStyle(
	    D2D1::StrokeStyleProperties(
	        D2D1_CAP_STYLE_ROUND,
	        D2D1_CAP_STYLE_ROUND,
	        D2D1_CAP_STYLE_ROUND,
	        D2D1_LINE_JOIN_ROUND,
	        0.0f,
	        D2D1_DASH_STYLE_SOLID,
	        0.0f
	    ),
	    TNULL,
	    0,
	    &m_pStrokeStyle
	);

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
				a_rCharInfo.flWidth  = it->pTreeNode->width;
				a_rCharInfo.flHeight = it->pTreeNode->height;
				a_rCharInfo.flUV1X   = it->flUV1X;
				a_rCharInfo.flUV1Y   = it->flUV1Y;
				a_rCharInfo.flUV2X   = it->flUV2X;
				a_rCharInfo.flUV2Y   = it->flUV2Y;

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
	TFLOAT flOutlineSize = GetOutlineSize( a_flScale );
	//flOutlineSize        = 0.0f;
	TFLOAT flScaleX      = remaster::g_pUIRender->GetScaleX();
	TFLOAT flScaleY      = remaster::g_pUIRender->GetScaleY();
	TFLOAT flCharWidth   = remaster::fontcache::GetTextWidth( remaster::font::GetFont( 0 ), &a_wChar, 1, flFontSize ) * flScaleY + m_flSpriteMargin * 2 + flOutlineSize * 2;
	TFLOAT flCharHeight  = remaster::fontcache::GetTextHeight( remaster::font::GetFont( 0 ), &a_wChar, 1, flFontSize ) * flScaleY + m_flSpriteMargin * 2 + flOutlineSize * 2;

	ID2D1Geometry*        pTextGeometry = remaster::dx11::CreateTextGeometry( remaster::font::GetFont( 0 ), &a_wChar, 1, flFontSize );
	ID2D1SolidColorBrush* pOutlineBrush = remaster::fontcache::GetSolidColorBrush( TCOLOR4( 0, 0, 0, 255 ) );
	ID2D1SolidColorBrush* pColorBrush   = remaster::fontcache::GetSolidColorBrush( TCOLOR( 255, 255, 255 ) );

	PartitionTreeNode* pTreeNode     = PartitionTree_AddNode( &m_oPartitionTree, flCharWidth, flCharHeight );
	ID2D1RenderTarget* pRenderTarget = remaster::g_pRender->GetD2DRenderTarget();

	D2D1::Matrix3x2F transform =
	    D2D1::Matrix3x2F::Scale( flScaleY, flScaleY ) *
	    D2D1::Matrix3x2F::Translation( pTreeNode->x + m_flSpriteMargin + flOutlineSize, pTreeNode->y - m_flSpriteMargin - flOutlineSize + flCharHeight );

	pRenderTarget->BeginDraw();

	pRenderTarget->SetTransform( transform );
	pRenderTarget->DrawGeometry( pTextGeometry, pOutlineBrush, flOutlineSize, m_pStrokeStyle );
	pRenderTarget->FillGeometry( pTextGeometry, pColorBrush );
	pRenderTarget->EndDraw();
	pTextGeometry->Release();

	//-----------------------------------------------------------------------------
	// 2. Fill information of this cached character
	//-----------------------------------------------------------------------------

	ScaledChar oScaledChar;
	oScaledChar.pTreeNode = pTreeNode;
	oScaledChar.flScale   = flFontSize;
	oScaledChar.flUV1X    = ( pTreeNode->x ) / m_uiWidth;
	oScaledChar.flUV1Y    = ( pTreeNode->y ) / m_uiHeight;
	oScaledChar.flUV2X    = ( pTreeNode->x + flCharWidth ) / m_uiWidth;
	oScaledChar.flUV2Y    = ( pTreeNode->y + flCharHeight ) / m_uiHeight;
	m_vecCachedChars[ rCharIndex ]->m_vecChars.Push( oScaledChar );

	a_rCharInfo.flWidth  = oScaledChar.pTreeNode->width;
	a_rCharInfo.flHeight = oScaledChar.pTreeNode->height;
	a_rCharInfo.flUV1X   = oScaledChar.flUV1X;
	a_rCharInfo.flUV1Y   = oScaledChar.flUV1Y;
	a_rCharInfo.flUV2X   = oScaledChar.flUV2X;
	a_rCharInfo.flUV2Y   = oScaledChar.flUV2Y;
}

TFLOAT remaster::FontAtlas::GetTextWidth( const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flScale )
{
	TFLOAT flResult       = 0.0f;
	TFLOAT flScaleXFactor = 1.0f / remaster::g_pUIRender->GetScaleX();
	TFLOAT flOutlineSize  = GetOutlineSize( a_flScale );

	for ( TSIZE i = 0; i < a_uiTextLength; i++ )
	{
		remaster::FontAtlas::CharInfo oCharInfo;
		GetCharUV( a_wcsText[ i ], a_flScale, oCharInfo );

		flResult += oCharInfo.flWidth * flScaleXFactor - flOutlineSize - GetSpriteMargin();
	}

	return flResult;
}

TFLOAT remaster::FontAtlas::GetTextHeight( const TWCHAR* a_wcsText, TSIZE a_uiTextLength, TFLOAT a_flScale )
{
	TFLOAT flResult       = 0.0f;
	TFLOAT flScaleYFactor = 1.0f / remaster::g_pUIRender->GetScaleY();
	TFLOAT flOutlineSize  = GetOutlineSize( a_flScale );

	for ( TSIZE i = 0; i < a_uiTextLength; i++ )
	{
		remaster::FontAtlas::CharInfo oCharInfo;
		GetCharUV( a_wcsText[ i ], a_flScale, oCharInfo );

		flResult = TMath::Max( flResult, oCharInfo.flHeight * flScaleYFactor - flOutlineSize - GetSpriteMargin() );
	}

	return flResult;
}
