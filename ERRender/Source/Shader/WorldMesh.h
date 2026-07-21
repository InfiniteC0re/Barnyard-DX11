#pragma once
#include "Ref/AWorldShader/AWorldMesh_DX8.h"

namespace remaster
{

class WorldMesh
    : public AWorldMesh
{
public:
	TDECLARE_CLASS( WorldMesh, AWorldMesh );

public:
	WorldMesh();
	~WorldMesh();

	//-----------------------------------------------------------------------------
	// Toshi::TMesh
	//-----------------------------------------------------------------------------
	virtual TBOOL Render() OVERRIDE;

	void SetWorldBoundsCentre( TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fZ )
	{
		m_oWorldBoundsCentre.Set( a_fX, a_fY, a_fZ );
		m_bHasWorldBounds = TTRUE;
	}

	TBOOL                  HasWorldBounds() const { return m_bHasWorldBounds; }
	const Toshi::TVector3& GetWorldBoundsCentre() const { return m_oWorldBoundsCentre; }

private:
	Toshi::TVector3 m_oWorldBoundsCentre;
	TBOOL           m_bHasWorldBounds;
};

} // namespace remaster
