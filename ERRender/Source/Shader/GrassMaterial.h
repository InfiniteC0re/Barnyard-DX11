#pragma once
#include "Ref/AGrassShader/AGrassMaterial.h"

namespace remaster
{

class GrassMaterial
    : public AGrassMaterial
{
public:
	TDECLARE_CLASS( GrassMaterial, AGrassMaterial );

public:
	GrassMaterial();
	~GrassMaterial();

	//-----------------------------------------------------------------------------
	// Toshi::TMaterial
	//-----------------------------------------------------------------------------
	virtual void PreRender() OVERRIDE;
	virtual void PostRender() OVERRIDE;
};

} // namespace remaster
