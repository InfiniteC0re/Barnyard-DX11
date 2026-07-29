#pragma once
#include "RenderDX11.h"

#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Types.h>

namespace remaster
{

class FontAtlas;

// Routes RmlUi text through the game's SDF FontAtlas instead of RmlUi's built-in
// FreeType engine. Families "Rekord26" and "Rekord18" map to the two game fonts
class RmlSdfFontEngine : public Rml::FontEngineInterface
{
public:
	struct FontFace
	{
		FontAtlas*             pAtlas;
		RenderDX11::FONT       eFontIndex;
		TINT                   iSize;
		TFLOAT                 flScale;
		Rml::FontMetrics       oMetrics;
	};

	RmlSdfFontEngine();
	~RmlSdfFontEngine();

	virtual bool LoadFontFace( const Rml::String& a_rFileName, int a_iFaceIndex, bool a_bFallbackFace, Rml::Style::FontWeight a_eWeight ) OVERRIDE;
	virtual bool LoadFontFace( Rml::Span<const Rml::byte> a_Data, int a_iFaceIndex, const Rml::String& a_rFamily, Rml::Style::FontStyle a_eStyle, Rml::Style::FontWeight a_eWeight, bool a_bFallbackFace ) OVERRIDE;

	virtual Rml::FontFaceHandle GetFontFaceHandle( const Rml::String& a_rFamily, Rml::Style::FontStyle a_eStyle, Rml::Style::FontWeight a_eWeight, int a_iSize ) OVERRIDE;
	virtual Rml::FontEffectsHandle PrepareFontEffects( Rml::FontFaceHandle a_Handle, const Rml::FontEffectList& a_rFontEffects ) OVERRIDE;
	virtual const Rml::FontMetrics& GetFontMetrics( Rml::FontFaceHandle a_Handle ) OVERRIDE;

	virtual int GetStringWidth( Rml::FontFaceHandle a_Handle, Rml::StringView a_String, const Rml::TextShapingContext& a_rShaping, Rml::Character a_PriorCharacter ) OVERRIDE;

	virtual int GenerateString( Rml::RenderManager& a_rRenderManager, Rml::FontFaceHandle a_Handle, Rml::FontEffectsHandle a_EffectsHandle, Rml::StringView a_String,
	    Rml::Vector2f a_vPosition, Rml::ColourbPremultiplied a_Colour, float a_flOpacity, const Rml::TextShapingContext& a_rShaping, Rml::TexturedMeshList& a_rMeshList ) OVERRIDE;

	virtual int GetVersion( Rml::FontFaceHandle a_Handle ) OVERRIDE;
	virtual void ReleaseFontResources() OVERRIDE;

	// Font-effect offsets (e.g. shadow) are parse-time px with the dp unit dropped,
	// so scale them by the live dp ratio to keep them tracking the scaled text
	void SetDpRatio( TFLOAT a_flRatio ) { m_flDpRatio = a_flRatio; }

private:
	Rml::CallbackTextureSource& GetTextureSource( RenderDX11::FONT a_eFontIndex );

private:
	Rml::Vector<FontFace*>            m_vecFaces;
	Rml::Vector<Rml::FontEffectList*> m_vecEffectLists;
	Rml::CallbackTextureSource        m_aTextureSources[ RenderDX11::FONT_NUMOF ];
	TBOOL                             m_abSourceReady[ RenderDX11::FONT_NUMOF ] = {};
	TFLOAT                            m_flDpRatio = 1.0f;
};

} // namespace remaster
