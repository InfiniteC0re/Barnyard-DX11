#pragma once
#include <Render/TRenderContext.h>
#include <StaticLights.h>

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

	// Per-cell static light IDs; -1 = empty slot
	void         ClearStaticLightIDs()
	{
		for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
			m_aStaticLightIds[ i ] = -1;
	}
	void         AddStaticLight( TINT8 a_iLightId )
	{
		for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
		{
			if ( m_aStaticLightIds[ i ] == -1 )
			{
				m_aStaticLightIds[ i ] = a_iLightId;
				return;
			}
		}
	}
	const TINT8* GetStaticLightIDs() const { return m_aStaticLightIds; }

private:
	Toshi::TMatrix44 m_Projection;
	TINT8            m_aStaticLightIds[ MAX_CELL_STATIC_LIGHTS ];
};

}