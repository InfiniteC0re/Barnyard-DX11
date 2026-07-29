#include "pch.h"
#include "RmlSdfFontEngine.h"
#include "UI/FontAtlas.h"

#undef GetFirstChild
#undef GetNextSibling
#undef GetPrevSibling
#undef GetNextWindow

#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/FontEffect.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster
{

// FontAtlas rasterises glyphs at this em size (FT_Set_Pixel_Sizes), so a glyph
// covering N atlas pixels renders at N * (size/ATLAS_EM) output pixels
static constexpr TFLOAT ATLAS_EM = 32.0f;

static TBOOL FamilyToFontIndex( const Rml::String& a_rFamily, RenderDX11::FONT& a_reOut )
{
	const Rml::String strLower = Rml::StringUtilities::ToLower( a_rFamily );

	if ( strLower == "rekord26" ) { a_reOut = RenderDX11::FONT_REKORD26; return TTRUE; }
	if ( strLower == "rekord18" ) { a_reOut = RenderDX11::FONT_REKORD18; return TTRUE; }
	// Give the debugger overlay a readable font
	if ( strLower == "rmlui-debugger-font" ) { a_reOut = RenderDX11::FONT_REKORD18; return TTRUE; }

	return TFALSE;
}

RmlSdfFontEngine::RmlSdfFontEngine() = default;

RmlSdfFontEngine::~RmlSdfFontEngine()
{
	ReleaseFontResources();
}

bool RmlSdfFontEngine::LoadFontFace( const Rml::String& a_rFileName, int, bool, Rml::Style::FontWeight )
{
	RenderDX11::FONT eFont;
	return FamilyToFontIndex( a_rFileName, eFont );
}

bool RmlSdfFontEngine::LoadFontFace( Rml::Span<const Rml::byte>, int, const Rml::String& a_rFamily, Rml::Style::FontStyle, Rml::Style::FontWeight, bool )
{
	RenderDX11::FONT eFont;
	return FamilyToFontIndex( a_rFamily, eFont );
}

Rml::FontFaceHandle RmlSdfFontEngine::GetFontFaceHandle( const Rml::String& a_rFamily, Rml::Style::FontStyle, Rml::Style::FontWeight, int a_iSize )
{
	RenderDX11::FONT eFont;
	if ( !FamilyToFontIndex( a_rFamily, eFont ) )
		return 0;

	for ( FontFace* pFace : m_vecFaces )
		if ( pFace->eFontIndex == eFont && pFace->iSize == a_iSize )
			return Rml::FontFaceHandle( pFace );

	FontAtlas* pAtlas = g_pRender->GetFontAtlas( eFont );
	if ( !pAtlas )
		return 0;

	FontFace* pFace   = new FontFace();
	pFace->pAtlas     = pAtlas;
	pFace->eFontIndex = eFont;
	pFace->iSize      = a_iSize;
	pFace->flScale    = a_iSize / ( ATLAS_EM * pAtlas->GetBaseScale() );

	// Baseline distance from the top of the line (lineHeight * baseLine). The
	// game's PositionOffsetY hack is intentionally left out; it only existed to
	// line up with the original bitmap fonts
	const TFLOAT flAscent = pAtlas->GetLineHeight() * pAtlas->GetBaseLine() * pFace->flScale;
	// The line box hugs the baseline; descenders (g, p, y) overrun it into the
	// margin, which suits the game's caps-heavy UI
	const TFLOAT flDescent = 0.0f;

	pFace->oMetrics.size                = a_iSize;
	pFace->oMetrics.line_spacing        = flAscent + flDescent;
	pFace->oMetrics.ascent              = flAscent;
	pFace->oMetrics.descent             = flDescent;
	pFace->oMetrics.x_height            = pFace->oMetrics.ascent * 0.5f;
	pFace->oMetrics.underline_position  = pFace->oMetrics.descent * 0.5f;
	pFace->oMetrics.underline_thickness = TMath::Max( 1.0f, a_iSize / 16.0f );
	pFace->oMetrics.has_ellipsis        = false;

	m_vecFaces.push_back( pFace );
	return Rml::FontFaceHandle( pFace );
}

Rml::FontEffectsHandle RmlSdfFontEngine::PrepareFontEffects( Rml::FontFaceHandle, const Rml::FontEffectList& a_rFontEffects )
{
	if ( a_rFontEffects.empty() )
		return 0;

	// Identical effect definitions share one FontEffect instance (RmlUi's factory
	// caches them by fingerprint), so a matching pointer sequence is the same
	// combination. Reuse a cached list instead of leaking a fresh one every relayout
	for ( Rml::FontEffectList* pExisting : m_vecEffectLists )
	{
		if ( pExisting->size() != a_rFontEffects.size() )
			continue;

		TBOOL bMatch = TTRUE;
		for ( TSIZE i = 0; i < a_rFontEffects.size(); i++ )
			if ( ( *pExisting )[ i ].get() != a_rFontEffects[ i ].get() )
			{
				bMatch = TFALSE;
				break;
			}

		if ( bMatch )
			return Rml::FontEffectsHandle( pExisting );
	}

	Rml::FontEffectList* pList = new Rml::FontEffectList( a_rFontEffects );
	m_vecEffectLists.push_back( pList );
	return Rml::FontEffectsHandle( pList );
}

const Rml::FontMetrics& RmlSdfFontEngine::GetFontMetrics( Rml::FontFaceHandle a_Handle )
{
	return TREINTERPRETCAST( FontFace*, a_Handle )->oMetrics;
}

int RmlSdfFontEngine::GetStringWidth( Rml::FontFaceHandle a_Handle, Rml::StringView a_String, const Rml::TextShapingContext&, Rml::Character )
{
	FontFace* pFace = TREINTERPRETCAST( FontFace*, a_Handle );

	TINT                iWidth = 0;
	FontAtlas::CharInfo oCharInfo;

	for ( Rml::StringIteratorU8 it( a_String ); it; ++it )
	{
		pFace->pAtlas->GetCharUV( TWCHAR( char32_t( *it ) ), pFace->flScale, oCharInfo );
		iWidth += ( oCharInfo.iAdvanceX >> 6 );
	}

	return iWidth;
}

// Emit one line of glyph quads into a mesh at the given baseline and colour
static int AppendGlyphs( Rml::Mesh& a_rMesh, FontAtlas* a_pAtlas, TFLOAT a_flScale, Rml::StringView a_String, Rml::Vector2f a_vPosition, Rml::ColourbPremultiplied a_Colour )
{
	a_rMesh.vertices.reserve( a_rMesh.vertices.size() + a_String.size() * 4 );
	a_rMesh.indices.reserve( a_rMesh.indices.size() + a_String.size() * 6 );

	TFLOAT              flPenX = a_vPosition.x;
	FontAtlas::CharInfo oCharInfo;

	for ( Rml::StringIteratorU8 it( a_String ); it; ++it )
	{
		a_pAtlas->GetCharUV( TWCHAR( char32_t( *it ) ), a_flScale, oCharInfo );

		if ( oCharInfo.flWidth > 0.0f && oCharInfo.flHeight > 0.0f )
		{
			// position is the baseline; bearingY is the rise above it
			const TFLOAT flLeft   = flPenX + oCharInfo.iBearingX;
			const TFLOAT flTop    = a_vPosition.y - oCharInfo.iBearingY;
			const TFLOAT flRight  = flLeft + oCharInfo.flWidth;
			const TFLOAT flBottom = flTop + oCharInfo.flHeight;

			const int iBase = (int)a_rMesh.vertices.size();
			a_rMesh.vertices.resize( iBase + 4 );
			Rml::Vertex* pV = &a_rMesh.vertices[ iBase ];

			pV[ 0 ].position = { flLeft, flTop };     pV[ 0 ].colour = a_Colour; pV[ 0 ].tex_coord = { oCharInfo.flUV1X, oCharInfo.flUV1Y };
			pV[ 1 ].position = { flRight, flTop };    pV[ 1 ].colour = a_Colour; pV[ 1 ].tex_coord = { oCharInfo.flUV2X, oCharInfo.flUV1Y };
			pV[ 2 ].position = { flRight, flBottom };  pV[ 2 ].colour = a_Colour; pV[ 2 ].tex_coord = { oCharInfo.flUV2X, oCharInfo.flUV2Y };
			pV[ 3 ].position = { flLeft, flBottom };   pV[ 3 ].colour = a_Colour; pV[ 3 ].tex_coord = { oCharInfo.flUV1X, oCharInfo.flUV2Y };

			a_rMesh.indices.push_back( iBase + 0 );
			a_rMesh.indices.push_back( iBase + 1 );
			a_rMesh.indices.push_back( iBase + 2 );
			a_rMesh.indices.push_back( iBase + 0 );
			a_rMesh.indices.push_back( iBase + 2 );
			a_rMesh.indices.push_back( iBase + 3 );
		}

		flPenX += ( oCharInfo.iAdvanceX >> 6 );
	}

	return TINT( flPenX - a_vPosition.x );
}

int RmlSdfFontEngine::GenerateString( Rml::RenderManager& a_rRenderManager, Rml::FontFaceHandle a_Handle, Rml::FontEffectsHandle a_EffectsHandle, Rml::StringView a_String,
    Rml::Vector2f a_vPosition, Rml::ColourbPremultiplied a_Colour, float a_flOpacity, const Rml::TextShapingContext&, Rml::TexturedMeshList& a_rMeshList )
{
	FontFace*          pFace    = TREINTERPRETCAST( FontFace*, a_Handle );
	const Rml::Texture oTexture = GetTextureSource( pFace->eFontIndex ).GetTexture( a_rRenderManager );

	// Back-layer effects (e.g. shadow) reuse the same SDF glyphs, offset and tinted,
	// drawn behind the text. Effects that need a baked texture (outline, glow, blur)
	// are skipped since our atlas is fixed
	if ( a_EffectsHandle )
	{
		const Rml::FontEffectList& rEffects = *TREINTERPRETCAST( Rml::FontEffectList*, a_EffectsHandle );

		for ( const Rml::SharedPtr<const Rml::FontEffect>& rpEffect : rEffects )
		{
			if ( !rpEffect || rpEffect->GetLayer() != Rml::FontEffect::Layer::Back || rpEffect->HasUniqueTexture() )
				continue;

			Rml::Vector2i oOrigin( 0, 0 ), oDims( 0, 0 );
			rpEffect->GetGlyphMetrics( oOrigin, oDims, Rml::FontGlyph() );

			// The offset arrives as raw dp (unit dropped at parse time), so scale it
			// to px here so it tracks the text instead of growing at lower resolutions
			const TFLOAT flOffsetX = oOrigin.x * m_flDpRatio;
			const TFLOAT flOffsetY = oOrigin.y * m_flDpRatio;

			const Rml::ColourbPremultiplied oColour = rpEffect->GetColour().ToPremultiplied( a_flOpacity );

			a_rMeshList.resize( a_rMeshList.size() + 1 );
			a_rMeshList.back().texture = oTexture;
			AppendGlyphs( a_rMeshList.back().mesh, pFace->pAtlas, pFace->flScale,
			    a_String, Rml::Vector2f( a_vPosition.x + flOffsetX, a_vPosition.y + flOffsetY ), oColour );
		}
	}

	a_rMeshList.resize( a_rMeshList.size() + 1 );
	a_rMeshList.back().texture = oTexture;
	return AppendGlyphs( a_rMeshList.back().mesh, pFace->pAtlas, pFace->flScale, a_String, a_vPosition, a_Colour );
}

int RmlSdfFontEngine::GetVersion( Rml::FontFaceHandle )
{
	// Glyph UVs are stable once cached, so geometry never needs regenerating
	return 0;
}

void RmlSdfFontEngine::ReleaseFontResources()
{
	for ( FontFace* pFace : m_vecFaces )
		delete pFace;

	m_vecFaces.clear();

	for ( Rml::FontEffectList* pList : m_vecEffectLists )
		delete pList;

	m_vecEffectLists.clear();
}

Rml::CallbackTextureSource& RmlSdfFontEngine::GetTextureSource( RenderDX11::FONT a_eFontIndex )
{
	if ( !m_abSourceReady[ a_eFontIndex ] )
	{
		FontAtlas* pAtlas = g_pRender->GetFontAtlas( a_eFontIndex );

		m_aTextureSources[ a_eFontIndex ] = Rml::CallbackTextureSource(
		    [ pAtlas ]( const Rml::CallbackTextureInterface& a_rTexture ) -> bool
		    {
			    ID3D11ShaderResourceView* pSRV = pAtlas->GetTextureResource();
			    // Hand RmlUi the existing atlas SRV; the AddRef balances the
			    // Release RmlUi issues when it drops the callback texture
			    pSRV->AddRef();
			    a_rTexture.SetTextureHandle( Rml::TextureHandle( pSRV ), Rml::Vector2i( 1024, 1024 ) );
			    return true;
		    }
		);

		m_abSourceReady[ a_eFontIndex ] = TTRUE;
	}

	return m_aTextureSources[ a_eFontIndex ];
}

} // namespace remaster
