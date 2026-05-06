#pragma once
#include "Ref/ASkinShader/ASkinMesh_DX8.h"

namespace remaster
{

class SkinMesh
    : public ASkinMesh
{
public:
	TDECLARE_CLASS( SkinMesh, ASkinMesh );

public:
	SkinMesh();
	~SkinMesh();

	//-----------------------------------------------------------------------------
	// Toshi::TMesh
	//-----------------------------------------------------------------------------
	virtual TBOOL Render() OVERRIDE;

	TBOOL IsFOB() const { return m_bIsFOB; }
	void  SetIsFOB( TBOOL a_bFOB ) { m_bIsFOB = a_bFOB; }

private:
	TBOOL m_bIsFOB;
};

} // namespace remaster
