#pragma once
#include "Ref/ASysShader/ASysMesh.h"

namespace remaster
{

class SysMesh
	: public ASysMesh
{
public:
	SysMesh();
	~SysMesh();

	//-----------------------------------------------------------------------------
	// Toshi::TMesh
	//-----------------------------------------------------------------------------
	virtual TBOOL Render() OVERRIDE;

	//-----------------------------------------------------------------------------
	// ASysMesh
	//-----------------------------------------------------------------------------
	virtual void SetZBias( TINT a_iZBias ) OVERRIDE;

	TINT GetZBias() const { return m_iZBias; }

public:
	inline static BOOL ms_bStopRendering = TFALSE;

private:
	TINT m_iZBias;
};

} // namespace remaster
