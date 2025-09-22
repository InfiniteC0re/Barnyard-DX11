#include "pch.h"
#include "RenderContentDX11.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster
{

RenderContextD3D11::RenderContextD3D11( TRenderInterface* a_pRenderer )
    : Toshi::TRenderContext( a_pRenderer )
{
}

RenderContextD3D11::~RenderContextD3D11()
{
}

void RenderContextD3D11::Update()
{
	if ( IsDirty() )
	{
		if ( m_eCameraMode == CameraMode_Perspective )
		{
			ComputePerspectiveProjection();
			ComputePerspectiveFrustum();
		}
		else
		{
			ComputeOrthographicProjection();
			ComputeOrthographicFrustum();
		}

		/*auto pRenderer = TSTATICCAST( TRenderD3DInterface, m_pRenderer );
		pRenderer->GetDirect3DDevice()->SetTransform( D3DTS_VIEW, TREINTERPRETCAST( D3DMATRIX*, &TMatrix44::IDENTITY ) );
		pRenderer->GetDirect3DDevice()->SetTransform( D3DTS_PROJECTION, TREINTERPRETCAST( D3DMATRIX*, &m_Projection ) );*/
	}
}

void RenderContextD3D11::ComputePerspectiveProjection()
{
	TRenderContext::ComputePerspectiveProjection( m_Projection, m_oViewportParams, m_oProjParams );
}

void RenderContextD3D11::ComputeOrthographicProjection()
{
	TRenderContext::ComputeOrthographicProjection( m_Projection, m_oViewportParams, m_oProjParams );
}

void RenderContextD3D11::ComputePerspectiveFrustum()
{
	TRenderContext::ComputePerspectiveFrustum( m_aFrustumPlanes1, m_oViewportParams, m_oProjParams );

	// Copy planes
	m_aFrustumPlanes2[ WORLDPLANE_LEFT ]   = m_aFrustumPlanes1[ WORLDPLANE_LEFT ];
	m_aFrustumPlanes2[ WORLDPLANE_RIGHT ]  = m_aFrustumPlanes1[ WORLDPLANE_RIGHT ];
	m_aFrustumPlanes2[ WORLDPLANE_BOTTOM ] = m_aFrustumPlanes1[ WORLDPLANE_BOTTOM ];
	m_aFrustumPlanes2[ WORLDPLANE_TOP ]    = m_aFrustumPlanes1[ WORLDPLANE_TOP ];
	m_aFrustumPlanes2[ WORLDPLANE_NEAR ]   = m_aFrustumPlanes1[ WORLDPLANE_NEAR ];
	m_aFrustumPlanes2[ WORLDPLANE_FAR ]    = m_aFrustumPlanes1[ WORLDPLANE_FAR ];
}

void RenderContextD3D11::ComputeOrthographicFrustum()
{
	TRenderContext::ComputeOrthographicFrustum( m_aFrustumPlanes1, m_oViewportParams, m_oProjParams );

	// Copy planes
	m_aFrustumPlanes2[ WORLDPLANE_LEFT ]   = m_aFrustumPlanes1[ WORLDPLANE_LEFT ];
	m_aFrustumPlanes2[ WORLDPLANE_RIGHT ]  = m_aFrustumPlanes1[ WORLDPLANE_RIGHT ];
	m_aFrustumPlanes2[ WORLDPLANE_BOTTOM ] = m_aFrustumPlanes1[ WORLDPLANE_BOTTOM ];
	m_aFrustumPlanes2[ WORLDPLANE_TOP ]    = m_aFrustumPlanes1[ WORLDPLANE_TOP ];
	m_aFrustumPlanes2[ WORLDPLANE_NEAR ]   = m_aFrustumPlanes1[ WORLDPLANE_NEAR ];
	m_aFrustumPlanes2[ WORLDPLANE_FAR ]    = m_aFrustumPlanes1[ WORLDPLANE_FAR ];
}

} // namespace remaster
