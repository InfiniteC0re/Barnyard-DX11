#pragma once
#include <Render/TRenderContext.h>

namespace remaster
{

class RenderContextD3D11 : public Toshi::TRenderContext
{
public:
	RenderContextD3D11( Toshi::TRenderInterface* a_pRenderer );
	~RenderContextD3D11();

	virtual void Update() override;

	void ComputePerspectiveProjection();
	void ComputeOrthographicProjection();

	void ComputePerspectiveFrustum();
	void ComputeOrthographicFrustum();

	const Toshi::TMatrix44& GetProjectionMatrix() const { return m_Projection; }

	void                       ClearStaticLightIDs() { m_oStaticLightIds.Reset(); }
	void                       AddStaticLight( Toshi::TLightID a_iLightId ) { m_oStaticLightIds.Add( a_iLightId ); }
	const Toshi::TLightIDList& GetStaticLightIDs() const { return m_oStaticLightIds; }

private:
	Toshi::TMatrix44    m_Projection;
	Toshi::TLightIDList m_oStaticLightIds;
};

}