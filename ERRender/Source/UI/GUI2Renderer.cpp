#include "pch.h"
#include "GUI2Renderer.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <BYardSDK/AMaterialLibraryManager.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x0064e5c0, AGUI2Renderer, AGUI2Renderer_Constructor, remaster::GUI2RendererDX11* )
{
	TSTATICASSERT( sizeof( remaster::GUI2RendererDX11 ) == 24 );

	return new ( this ) remaster::GUI2RendererDX11();
}

void remaster::SetupRenderHooks_UIRenderer()
{
	InstallHook<AGUI2Renderer_Constructor>();
}

remaster::GUI2RendererDX11::GUI2RendererDX11()
{
	m_pTransforms       = new AGUI2Transform[ MAX_NUM_TRANSFORMS ];
	m_iTransformCount   = 0;
	m_bIsTransformDirty = TFALSE;
}

remaster::GUI2RendererDX11::~GUI2RendererDX11()
{
	delete[] m_pTransforms;
}

AGUI2Material* remaster::GUI2RendererDX11::CreateMaterial( Toshi::TTexture* a_pTexture )
{
	auto pMaterial = new AGUI2Material;

	pMaterial->Create_Hack();
	pMaterial->m_pTextureResource = a_pTexture;

	return pMaterial;
}

AGUI2Material* remaster::GUI2RendererDX11::CreateMaterialFromName( const TCHAR* a_szTextureName )
{
	return CreateMaterial(
	    GetTexture( a_szTextureName )
	);
}

TUINT remaster::GUI2RendererDX11::GetWidth( AGUI2Material* a_pMaterial )
{
	return a_pMaterial->m_pTextureResource->GetWidth();
}

TUINT remaster::GUI2RendererDX11::GetHeight( AGUI2Material* a_pMaterial )
{
	return a_pMaterial->m_pTextureResource->GetHeight();
}

void remaster::GUI2RendererDX11::BeginScene()
{
	
}

void remaster::GUI2RendererDX11::EndScene()
{
	
}

void remaster::GUI2RendererDX11::ResetRenderer()
{
	
}

void remaster::GUI2RendererDX11::PrepareRenderer()
{
	
}

void remaster::GUI2RendererDX11::SetMaterial( AGUI2Material* a_pMaterial )
{
	if ( a_pMaterial == m_pMaterial ) return;

	m_pMaterial = a_pMaterial;
}

void remaster::GUI2RendererDX11::PushTransform( const AGUI2Transform& a_rTransform, const Toshi::TVector2& a_rVec1, const Toshi::TVector2& a_rVec2 )
{
	TASSERT( m_iTransformCount < MAX_NUM_TRANSFORMS );
	auto pTransform = m_pTransforms + ( m_iTransformCount++ );

	AGUI2Transform transform1 = *pTransform;
	AGUI2Transform transform2 = a_rTransform;

	TVector2 vec;
	transform1.Transform( vec, a_rVec1 );
	transform1.m_vecTranslation = { vec.x, vec.y };

	transform2.Transform( vec, a_rVec2 );
	transform2.m_vecTranslation = { vec.x, vec.y };

	AGUI2Transform::Multiply( m_pTransforms[ m_iTransformCount ], transform1, transform2 );
	m_bIsTransformDirty = TTRUE;
}

void remaster::GUI2RendererDX11::PopTransform()
{
	TASSERT( m_iTransformCount >= 0 );
	m_iTransformCount -= 1;
	m_bIsTransformDirty = TTRUE;
}

void remaster::GUI2RendererDX11::SetTransform( const AGUI2Transform& a_rTransform )
{
	m_pTransforms[ m_iTransformCount ] = a_rTransform;
	m_bIsTransformDirty                = TTRUE;
}

void remaster::GUI2RendererDX11::SetColour( TUINT32 a_uiColour )
{
	m_uiColour = a_uiColour;
}

void remaster::GUI2RendererDX11::SetScissor( TFLOAT a_fVal1, TFLOAT a_fVal2, TFLOAT a_fVal3, TFLOAT a_fVal4 )
{
	
}

void remaster::GUI2RendererDX11::ClearScissor()
{
	
}

void remaster::GUI2RendererDX11::RenderRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b, const Toshi::TVector2& uv1, const Toshi::TVector2& uv2 )
{
	
}

void remaster::GUI2RendererDX11::RenderTriStrip( Toshi::TVector2* vertices, Toshi::TVector2* UV, uint32_t numverts )
{
	
}

void remaster::GUI2RendererDX11::RenderLine( const Toshi::TVector2& a, const Toshi::TVector2& b )
{
	
}

void remaster::GUI2RendererDX11::RenderOutlineRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b )
{
	
}

void remaster::GUI2RendererDX11::RenderFilledRectangle( const Toshi::TVector2& a, const Toshi::TVector2& b )
{
	
}

void remaster::GUI2RendererDX11::ScaleCoords( TFLOAT& x, TFLOAT& y )
{
	
}

void remaster::GUI2RendererDX11::ResetZCoordinate()
{
	sm_fZCoordinate = 0.01f;
}

void remaster::GUI2RendererDX11::UpdateTransform()
{
	//auto            pRender    = TSTATICCAST( TRenderD3DInterface, TRenderInterface::GetSingleton() );
	AGUI2Transform* pTransform = m_pTransforms + m_iTransformCount;

	TMatrix44 worldMatrix;
	worldMatrix.m_f11 = pTransform->m_aMatrixRows[ 0 ].x;
	worldMatrix.m_f12 = pTransform->m_aMatrixRows[ 0 ].y;
	worldMatrix.m_f13 = 0.0f;
	worldMatrix.m_f14 = 0.0f;

	worldMatrix.m_f21 = pTransform->m_aMatrixRows[ 1 ].x;
	worldMatrix.m_f22 = pTransform->m_aMatrixRows[ 1 ].y;
	worldMatrix.m_f23 = 0.0f;
	worldMatrix.m_f24 = 0.0f;

	worldMatrix.m_f31 = 0.0f;
	worldMatrix.m_f32 = 0.0f;
	worldMatrix.m_f33 = 1.0f;
	worldMatrix.m_f34 = 0.0f;

	worldMatrix.m_f41 = pTransform->m_vecTranslation.x;
	worldMatrix.m_f42 = pTransform->m_vecTranslation.y;
	worldMatrix.m_f43 = 0.0f;
	worldMatrix.m_f44 = 1.0f;

	//pRender->GetDirect3DDevice()->SetTransform( D3DTS_WORLDMATRIX( 0 ), (D3DMATRIX*)&worldMatrix );
	m_bIsTransformDirty = TFALSE;
}

void remaster::GUI2RendererDX11::SetupProjectionMatrix( Toshi::TMatrix44& a_rOutMatrix, TINT a_iLeft, TINT a_iRight, TINT a_iTop, TINT a_iBottom )
{
	/*auto pRender        = TSTATICCAST( TRenderD3DInterface, TRenderInterface::GetSingleton() );
	auto pDisplayParams = pRender->GetCurrentDisplayParams();

	a_rOutMatrix = {
		2.0f / ( a_iRight - a_iLeft ), 0.0f, 0.0f, 0.0f,
		0.0f, 2.0f / ( a_iBottom - a_iTop ), 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		( ( pDisplayParams->uiWidth - a_iLeft ) - a_iRight ) / (TFLOAT)( a_iRight - a_iLeft ), ( ( pDisplayParams->uiHeight - a_iTop ) - a_iBottom ) / (TFLOAT)( a_iTop - a_iBottom ), 0.0f, 1.0f
	};*/
}

void remaster::GUI2RendererDX11::RenderLine( TFLOAT x1, TFLOAT y1, TFLOAT x2, TFLOAT y2 )
{
	
}

void remaster::GUI2RendererDX11::DestroyMaterial( AGUI2Material* a_pMaterial )
{
	if ( a_pMaterial )
		a_pMaterial->Destroy();
}

Toshi::TTexture* remaster::GUI2RendererDX11::GetTexture( const TCHAR* a_szTextureName )
{
	return AMaterialLibraryManager::GetSingleton()->FindTexture( a_szTextureName );
}
