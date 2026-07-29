#pragma once
#include "RenderDX11.h"

#include <RmlUi/Core/RenderInterface.h>
#include <Math/TMatrix44.h>
#include <ToshiTools/T2DynamicVector.h>

namespace remaster
{

class RmlRenderInterfaceDX11 : public Rml::RenderInterface
{
public:
	TBOOL Create();
	void  Destroy();

	// Set projection and shared render states for the whole context render pass
	void BeginFrame();
	void EndFrame();

	// Font-atlas SRVs render through the SDF shader instead of the plain textured one
	void RegisterFontTexture( ID3D11ShaderResourceView* a_pTexture );

	virtual Rml::CompiledGeometryHandle CompileGeometry( Rml::Span<const Rml::Vertex> a_Vertices, Rml::Span<const int> a_Indices ) OVERRIDE;
	virtual void                        RenderGeometry( Rml::CompiledGeometryHandle a_Geometry, Rml::Vector2f a_vTranslation, Rml::TextureHandle a_Texture ) OVERRIDE;
	virtual void                        ReleaseGeometry( Rml::CompiledGeometryHandle a_Geometry ) OVERRIDE;

	virtual Rml::TextureHandle LoadTexture( Rml::Vector2i& a_rDimensions, const Rml::String& a_rSource ) OVERRIDE;
	virtual Rml::TextureHandle GenerateTexture( Rml::Span<const Rml::byte> a_Source, Rml::Vector2i a_Dimensions ) OVERRIDE;
	virtual void               ReleaseTexture( Rml::TextureHandle a_Texture ) OVERRIDE;

	virtual void EnableScissorRegion( bool a_bEnable ) OVERRIDE;
	virtual void SetScissorRegion( Rml::Rectanglei a_Region ) OVERRIDE;

	virtual void SetTransform( const Rml::Matrix4f* a_pTransform ) OVERRIDE;

private:
	Rml::TextureHandle CreateTextureRGBA( const void* a_pData, TINT a_iWidth, TINT a_iHeight );
	TBOOL              IsFontTexture( ID3D11ShaderResourceView* a_pTexture ) const;

private:
	Toshi::T2DynamicVector<ID3D11ShaderResourceView*>      m_vecFontTextures;
	Toshi::T2DynamicVector<RenderDX11::ShaderPipelineState> m_vecPipelines;
	ID3D11InputLayout*                                      m_pInputLayout = TNULL;
	Toshi::TMatrix44                                        m_matProjection;
	Toshi::TMatrix44                                        m_matTransform;
	TBOOL                                                   m_bHasTransform  = TFALSE;
	D3D11_VIEWPORT                                          m_oSavedViewport = {};
};

} // namespace remaster
