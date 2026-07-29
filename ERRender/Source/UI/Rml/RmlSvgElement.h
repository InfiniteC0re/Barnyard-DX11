#pragma once
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/CallbackTexture.h>

struct NSVGimage;

namespace remaster
{

// Custom <svg src="..."> element: parses the file with nanosvg and rasterises it at
// the element's own pixel size, re-rasterising on resize so it stays crisp at any
// scale (unlike the render-interface .svg path, which bakes a fixed-size texture).
// Register once with RegisterSvgElement() before any document is loaded
class RmlSvgElement : public Rml::Element
{
public:
	RmlSvgElement( const Rml::String& a_rTag );
	~RmlSvgElement() OVERRIDE;

	bool GetIntrinsicDimensions( Rml::Vector2f& a_rDimensions, float& a_rRatio ) OVERRIDE;

protected:
	void OnRender() OVERRIDE;
	void OnResize() OVERRIDE;
	void OnAttributeChange( const Rml::ElementAttributes& a_rChangedAttributes ) OVERRIDE;
	void OnPropertyChange( const Rml::PropertyIdSet& a_rChangedProperties ) OVERRIDE;

private:
	void LoadSource();
	void UpdateTexture();
	void GenerateGeometry();

	NSVGimage*                 m_pImage    = TNULL;
	Rml::Vector2f              m_vIntrinsic = Rml::Vector2f( 0, 0 );
	Rml::Vector2i              m_vTexDims   = Rml::Vector2i( 0, 0 );
	Rml::Vector<Rml::byte>     m_vecPixels;
	Rml::CallbackTextureSource m_oTextureSource;
	Rml::Geometry              m_oGeometry;
	TBOOL                      m_bSourceDirty   = TTRUE;
	TBOOL                      m_bTextureDirty  = TTRUE;
	TBOOL                      m_bGeometryDirty = TTRUE;
};

void RegisterSvgElement();

} // namespace remaster
