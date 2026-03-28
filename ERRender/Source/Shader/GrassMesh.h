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

	CellMeshSphere* GetCellMeshSphere() const { return m_pCellMeshSphere; }

private:
	TCHAR PADDING[ 136 ];

	void*           m_pUnk1;
	CellMeshSphere* m_pCellMeshSphere;
};

} // namespace remaster
