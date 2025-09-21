#pragma once
#include "Ref/AWorldShader/AWorldMesh_DX8.h"

namespace remaster
{

class WorldMesh
    : public AWorldMesh
{
public:
	WorldMesh();
	~WorldMesh();

	//-----------------------------------------------------------------------------
	// Toshi::TMesh
	//-----------------------------------------------------------------------------
	virtual TBOOL Render() OVERRIDE;
};

} // namespace remaster
