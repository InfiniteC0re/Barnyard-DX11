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

private:
	Toshi::TMatrix44 m_Projection;
};

}