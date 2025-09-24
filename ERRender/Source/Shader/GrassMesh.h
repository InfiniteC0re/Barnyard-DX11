#pragma once
#include "Ref/AGrassShader/AGrassMeshHAL_DX8.h"

namespace remaster
{

class GrassMesh
    : public AGrassMesh
{
public:
	TDECLARE_CLASS( GrassMesh, AGrassMesh );

public:
	GrassMesh();
	~GrassMesh();

	//-----------------------------------------------------------------------------
	// Toshi::TMesh
	//-----------------------------------------------------------------------------
	virtual TBOOL Render() OVERRIDE;

private:
	TCHAR PADDING[ 144 ];
};

} // namespace remaster
