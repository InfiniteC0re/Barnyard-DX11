#pragma once
#include "Ref/AGrassShader/AGrassMeshHAL_DX8.h"

namespace remaster
{

class GrassMesh
    : public AGrassMesh
{
public:
	GrassMesh();
	~GrassMesh();

	//-----------------------------------------------------------------------------
	// Toshi::TMesh
	//-----------------------------------------------------------------------------
	virtual TBOOL Render() OVERRIDE;
};

} // namespace remaster
