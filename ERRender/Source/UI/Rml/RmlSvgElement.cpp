#include "pch.h"
#include "RmlSvgElement.h"

#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/PropertyIdSet.h>

#include <nanosvg.h>
#include <nanosvgrast.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

namespace remaster
{

// UI is single-threaded, so one shared rasteriser is enough
static NSVGrasterizer* s_pRasterizer = TNULL;

RmlSvgElement::RmlSvgElement( const Rml::String& a_rTag )
    : Rml::Element( a_rTag )
{
}

RmlSvgElement::~RmlSvgElement()
{
	if ( m_pImage )
		nsvgDelete( m_pImage );
}

bool RmlSvgElement::GetIntrinsicDimensions( Rml::Vector2f& a_rDimensions, float& a_rRatio )
{
	if ( m_bSourceDirty )
		LoadSource();

	a_rDimensions = m_vIntrinsic;
	if ( m_vIntrinsic.y > 0.0f )
		a_rRatio = m_vIntrinsic.x / m_vIntrinsic.y;

	return true;
}

void RmlSvgElement::OnAttributeChange( const Rml::ElementAttributes& a_rChangedAttributes )
{
	Rml::Element::OnAttributeChange( a_rChangedAttributes );

	if ( a_rChangedAttributes.find( "src" ) != a_rChangedAttributes.end() )
	{
		m_bSourceDirty = TTRUE;
		DirtyLayout();
	}
}

void RmlSvgElement::OnResize()
{
	m_bTextureDirty  = TTRUE;
	m_bGeometryDirty = TTRUE;
}

void RmlSvgElement::OnPropertyChange( const Rml::PropertyIdSet& a_rChangedProperties )
{
	Rml::Element::OnPropertyChange( a_rChangedProperties );

	// The render interface has no opacity layers, so opacity and tint are baked into the
	// geometry colour -- rebuild it whenever they change (matching ElementText/ElementImage)
	if ( a_rChangedProperties.Contains( Rml::PropertyId::ImageColor ) || a_rChangedProperties.Contains( Rml::PropertyId::Opacity ) )
		m_bGeometryDirty = TTRUE;
}

void RmlSvgElement::OnRender()
{
	if ( m_bSourceDirty )
		LoadSource();

	if ( !m_pImage )
		return;

	if ( m_bTextureDirty )
		UpdateTexture();
	if ( m_bGeometryDirty )
		GenerateGeometry();

	if ( Rml::RenderManager* pRenderManager = GetRenderManager() )
		m_oGeometry.Render( GetAbsoluteOffset( Rml::BoxArea::Border ), m_oTextureSource.GetTexture( *pRenderManager ) );
}

void RmlSvgElement::LoadSource()
{
	m_bSourceDirty = TFALSE;

	if ( m_pImage )
	{
		nsvgDelete( m_pImage );
		m_pImage = TNULL;
	}
	m_vIntrinsic = Rml::Vector2f( 0, 0 );

	const Rml::String strSrc = GetAttribute<Rml::String>( "src", "" );
	if ( strSrc.empty() )
		return;

	// Resolve src relative to the document's directory. fopen (via nanosvg) handles the
	// ".." segments, and the game's UI files map straight to disk under the cwd
	Rml::String strPath = strSrc;
	if ( Rml::ElementDocument* pDocument = GetOwnerDocument() )
	{
		const Rml::String strDoc = pDocument->GetSourceURL();
		const Rml::String::size_type iSlash = strDoc.rfind( '/' );
		if ( iSlash != Rml::String::npos )
			strPath = strDoc.substr( 0, iSlash + 1 ) + strSrc;
	}

	m_pImage = nsvgParseFromFile( strPath.c_str(), "px", 96.0f );
	if ( m_pImage && m_pImage->width > 0.0f && m_pImage->height > 0.0f )
		m_vIntrinsic = Rml::Vector2f( m_pImage->width, m_pImage->height );

	m_bTextureDirty  = TTRUE;
	m_bGeometryDirty = TTRUE;
}

void RmlSvgElement::UpdateTexture()
{
	m_bTextureDirty = TFALSE;

	if ( !m_pImage )
		return;

	// Rasterise at the element's physical pixel size (content box * dp ratio)
	const Rml::Vector2f vFill = GetRenderBox( Rml::BoxArea::Content ).GetFillSize();
	const float         flDp  = Rml::ElementUtilities::GetDensityIndependentPixelRatio( this );

	TINT iWidth  = TINT( vFill.x * flDp + 0.5f );
	TINT iHeight = TINT( vFill.y * flDp + 0.5f );
	if ( iWidth < 1 || iHeight < 1 )
		return;

	constexpr TINT MAX_DIM = 2048;
	if ( iWidth > MAX_DIM ) iWidth = MAX_DIM;
	if ( iHeight > MAX_DIM ) iHeight = MAX_DIM;

	// The element is laid out to the SVG's aspect ratio, so a width-fit scale fills it
	const float flScale = iWidth / m_pImage->width;

	if ( !s_pRasterizer )
		s_pRasterizer = nsvgCreateRasterizer();

	m_vecPixels.assign( size_t( iWidth ) * iHeight * 4, Rml::byte( 0 ) );
	nsvgRasterize( s_pRasterizer, m_pImage, 0.0f, 0.0f, flScale, m_vecPixels.data(), iWidth, iHeight, iWidth * 4 );

	// nanosvg emits straight-alpha RGBA; Rml expects premultiplied
	for ( size_t i = 0; i + 3 < m_vecPixels.size(); i += 4 )
	{
		const TUINT uiA     = m_vecPixels[ i + 3 ];
		m_vecPixels[ i + 0 ] = Rml::byte( m_vecPixels[ i + 0 ] * uiA / 255 );
		m_vecPixels[ i + 1 ] = Rml::byte( m_vecPixels[ i + 1 ] * uiA / 255 );
		m_vecPixels[ i + 2 ] = Rml::byte( m_vecPixels[ i + 2 ] * uiA / 255 );
	}

	m_vTexDims = Rml::Vector2i( iWidth, iHeight );

	// A fresh source each resize; the old callback texture is released, the new one
	// uploads the just-rasterised pixels when first sampled
	m_oTextureSource = Rml::CallbackTextureSource(
	    [ this ]( const Rml::CallbackTextureInterface& a_rTexture ) -> bool
	    {
		    return a_rTexture.GenerateTexture( Rml::Span<const Rml::byte>( m_vecPixels.data(), m_vecPixels.size() ), m_vTexDims );
	    }
	);

	m_bGeometryDirty = TTRUE;
}

void RmlSvgElement::GenerateGeometry()
{
	m_bGeometryDirty = TFALSE;

	Rml::Mesh oMesh = m_oGeometry.Release( Rml::Geometry::ReleaseMode::ClearMesh );

	const Rml::ComputedValues&      rComputed = GetComputedValues();
	const Rml::ColourbPremultiplied oColour   = rComputed.image_color().ToPremultiplied( rComputed.opacity() );
	const Rml::RenderBox            oBox      = GetRenderBox( Rml::BoxArea::Content );

	Rml::MeshUtilities::GenerateQuad( oMesh, oBox.GetFillOffset(), oBox.GetFillSize(), oColour, Rml::Vector2f( 0, 0 ), Rml::Vector2f( 1, 1 ) );

	if ( Rml::RenderManager* pRenderManager = GetRenderManager() )
		m_oGeometry = pRenderManager->MakeGeometry( std::move( oMesh ) );
}

static Rml::ElementInstancerGeneric<RmlSvgElement> s_oSvgInstancer;

void RegisterSvgElement()
{
	Rml::Factory::RegisterElementInstancer( "svg", &s_oSvgInstancer );
}

} // namespace remaster
