#pragma once
#include "Ref/AGrassShader/AGrassShaderHAL_DX8.h"
#include "RenderDX11.h"

#include <d3d11.h>

namespace remaster
{

void SetupRenderHooks_GrassShader();

class GrassShaderDX11 :
	public AGrassShader
{
public:
	TDECLARE_CLASS( GrassShaderDX11, AGrassShader );

public:
	GrassShaderDX11();
	~GrassShaderDX11();

	//-----------------------------------------------------------------------------
	// Toshi::TShader
	//-----------------------------------------------------------------------------
	virtual void  Flush() OVERRIDE;
	virtual void  StartFlush() OVERRIDE;
	virtual void  EndFlush() OVERRIDE;
	virtual TBOOL Create() OVERRIDE;
	virtual TBOOL Validate() OVERRIDE;
	virtual void  Invalidate() OVERRIDE;
	virtual TBOOL TryInvalidate() OVERRIDE;
	virtual TBOOL TryValidate() OVERRIDE;
	virtual void  Render( Toshi::TRenderPacket* a_pRenderPacket ) OVERRIDE;

	//-----------------------------------------------------------------------------
	// AGrassShader
	//-----------------------------------------------------------------------------
	virtual AGrassMaterial* CreateMaterial( const TCHAR* a_szName ) OVERRIDE;
	virtual AGrassMesh*     CreateMesh( const TCHAR* a_szName ) OVERRIDE;

private:
	void UpdateAnimation();

private:
	Toshi::TOrderTable m_oOrderTable;
	TCHAR PADDING1[ 4 ];

	ID3DBlob* m_pVSShaderBlob;
	ID3DBlob* m_pPSShaderBlob;

	RenderDX11::ShaderPipelineState m_oShaderPipeline;
};

}; // namespace remaster
