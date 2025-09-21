#pragma once
#include "Ref/ASkinShader/ASkinMesh_DX8.h"

namespace remaster
{

class SkinMesh
    : public ASkinMesh
{
public:
	SkinMesh();
	~SkinMesh();

	//-----------------------------------------------------------------------------
	// Toshi::TMesh
	//-----------------------------------------------------------------------------
	virtual TBOOL Render() OVERRIDE;
};

} // namespace remaster
