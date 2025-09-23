#pragma once
#include <Math/TMatrix44.h>
#include <BYardSDK/AGUI2Renderer.h>

#include <d3d11.h>

namespace remaster
{

void SetupRenderHooks_UIRenderer();

class GUI2RendererDX11
    : public AGUI2Renderer
{
public:
	inline constexpr static TUINT32 MAX_NUM_TRANSFORMS = 32;
	inline constexpr static TUINT32 MAX_VERTICES       = 8;

	struct Vertex
	{
		Toshi::TVector3 Position;
		TUINT32         Colour;
		Toshi::TVector2 UV;
	};

public:
	GUI2RendererDX11();
	~GUI2RendererDX11();

	virtual AGUI2Material*   CreateMaterialFromName( const TCHAR* a_szTextureName ) OVERRIDE;
	virtual AGUI2Material*   CreateMaterial( Toshi::TTexture* a_pTexture ) OVERRIDE;
	virtual void             DestroyMaterial( AGUI2Material* a_pMaterial ) OVERRIDE;
	virtual Toshi::TTexture* GetTexture( const TCHAR* a_szTextureName ) OVERRIDE;
	virtual TUINT            GetWidth( AGUI2Material* a_pMaterial ) OVERRIDE;
	virtual TUINT            GetHeight( AGUI2Material* a_pMaterial ) OVERRIDE;
	virtual void             BeginScene() OVERRIDE;
	virtual void             EndScene() OVERRIDE;
	virtual void             ResetRenderer() OVERRIDE;
	virtual void             PrepareRenderer() OVERRIDE;
	virtual void             SetMaterial( AGUI2Material* a_pMaterial ) OVERRIDE;
	virtual void             PushTransform( const AGUI2Transform& a_rTransform, const Toshi::TVector2& a_rVec1, const Toshi::TVector2& a_rVec2 ) OVERRIDE;
	virtual void             PopTransform() OVERRIDE;
	virtual void             SetTransform( const AGUI2Transform& a_rTransform ) OVERRIDE;
	virtual void             SetColour( TUINT32 a_uiColour ) OVERRIDE;
	virtual void             SetScissor( TFLOAT a_fVal1, TFLOAT a_fVal2, TFLOAT a_fVal3, TFLOAT a_fVal4 ) OVERRIDE;
	virtual void             ClearScissor() OVERRIDE;
	virtual void             RenderRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b, const Toshi::TVector2& uv1, const Toshi::TVector2& uv2 ) OVERRIDE;
	virtual void             RenderTriStrip( Toshi::TVector2* vertices, Toshi::TVector2* UV, uint32_t numverts ) OVERRIDE;
	virtual void             RenderLine( const Toshi::TVector2& a, const Toshi::TVector2& b ) OVERRIDE;
	virtual void             RenderLine( TFLOAT x1, TFLOAT y1, TFLOAT x2, TFLOAT y2 ) OVERRIDE;
	virtual void             RenderOutlineRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b ) OVERRIDE;
	virtual void             RenderFilledRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b ) OVERRIDE;
	virtual void             ScaleCoords( TFLOAT& x, TFLOAT& y ) OVERRIDE;
	virtual void             ResetZCoordinate() OVERRIDE;

public:
	void UpdateTransform();

	const Toshi::TMatrix44& GetProjectionMatrix() const { return m_matProjection; }
	const Toshi::TMatrix44& GetViewMatrix() const { return m_matView; }

private:
	static void SetupProjectionMatrix( Toshi::TMatrix44& a_rOutMatrix, TFLOAT a_fLeft, TFLOAT a_fRight, TFLOAT a_fTop, TFLOAT a_fBottom );

public:
	inline static Vertex sm_Vertices[ MAX_VERTICES ];
	inline static TBOOL  sm_bUnknownFlag = TFALSE;
	inline static TFLOAT sm_fZCoordinate = 0.1f;

private:
	AGUI2Transform* m_pTransforms;
	TINT            m_iTransformCount;
	TUINT32         m_uiColour;
	TBOOL           m_bIsTransformDirty;
	AGUI2Material*  m_pMaterial;

	ID3D11InputLayout*  m_pInputLayout;
	ID3DBlob*           m_pVSShaderBlob;
	ID3D11VertexShader* m_pVertexShader;
	ID3DBlob*           m_pPSShaderBlob;
	ID3D11PixelShader*  m_pPixelShader;

	Toshi::TMatrix44 m_matProjection;
	Toshi::TMatrix44 m_matView;
};

}; // namespace remaster
